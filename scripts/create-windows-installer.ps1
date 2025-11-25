param (
    [Parameter(Mandatory)]
    [string]$SourceDir,
    [Parameter(Mandatory)]
    [string]$OutputExe,
    [string]$ProductName = "Smart Gamma"
)

function Find-FrameworkAssembly {
    param(
        [Parameter(Mandatory)]
        [string]$AssemblyFileName
    )

    $roots = @(
        (Join-Path ${env:ProgramFiles(x86)} "Reference Assemblies/Microsoft/Framework/.NETFramework"),
        (Join-Path ${env:ProgramFiles} "Reference Assemblies/Microsoft/Framework/.NETFramework"),
        (Join-Path ${env:WINDIR} "Microsoft.NET/assembly/GAC_MSIL")
    )
    $versions = @("v4.8","v4.7.2","v4.7.1","v4.7","v4.6.2","v4.6.1","v4.6","v4.5.2","v4.5.1","v4.5","v4.0")

    foreach ($root in $roots) {
        foreach ($version in $versions) {
            $candidate = Join-Path (Join-Path $root $version) $AssemblyFileName
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    return $null
}

if (-not (Test-Path $SourceDir)) {
    Write-Error "Source directory $SourceDir not found"
    exit 1
}

$resolvedSourceDir = (Resolve-Path -Path $SourceDir).ProviderPath

Write-Host "Preparing Smart Gamma installer..."

$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ([System.IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $tempDir | Out-Null

$resolvedOutputExe = if ([System.IO.Path]::IsPathRooted($OutputExe)) {
    [System.IO.Path]::GetFullPath($OutputExe)
} else {
    [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $OutputExe))
}
$outputDir = Split-Path -Parent $resolvedOutputExe
if ($outputDir -and -not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}
if (Test-Path $resolvedOutputExe) {
    Remove-Item -Force $resolvedOutputExe
}

try {
    $packageDir = Join-Path $tempDir "package"
    mkdir $packageDir | Out-Null

    $zipPath = Join-Path $packageDir "smart-gamma.zip"
    Write-Host "Compressing payload from $SourceDir..."
    Compress-Archive -Path (Join-Path $resolvedSourceDir "*") -DestinationPath $zipPath -Force
    $zipPath = [System.IO.Path]::GetFullPath($zipPath)

    $stubPath = Join-Path $tempDir "InstallerStub.cs"
    @'
using System;
using System.IO;
using System.IO.Compression;
using System.Reflection;

class Installer
{
    static void Main()
    {
        try
        {
            string destRoot = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "obs-studio");
            ExtractPayload(destRoot);
            Console.WriteLine("Smart Gamma installed to " + destRoot);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex.ToString());
        }
    }

    static void ExtractPayload(string destRoot)
    {
        Assembly assembly = Assembly.GetExecutingAssembly();
        using (Stream resource = assembly.GetManifestResourceStream("Payload.zip"))
        {
            if (resource == null)
                throw new InvalidOperationException("Missing embedded payload.");

            using (ZipArchive archive = new ZipArchive(resource))
            {
                string destRootFull = Path.GetFullPath(destRoot);
                foreach (ZipArchiveEntry entry in archive.Entries)
                {
                    string entryPath = entry.FullName ?? string.Empty;
                    string targetPath = Path.GetFullPath(Path.Combine(destRootFull, entryPath));
                    if (!targetPath.StartsWith(destRootFull, StringComparison.OrdinalIgnoreCase))
                        continue;

                    if (string.IsNullOrEmpty(entry.Name))
                    {
                        Directory.CreateDirectory(targetPath);
                        continue;
                    }

                    string dir = Path.GetDirectoryName(targetPath);
                    if (!string.IsNullOrEmpty(dir))
                        Directory.CreateDirectory(dir);

                    entry.ExtractToFile(targetPath, true);
                }
            }
        }
    }
}
'@ | Set-Content -Encoding UTF8 $stubPath

    $cscCandidates = @(
        (Join-Path $env:WINDIR "Microsoft.NET\Framework64\v4.0.30319\csc.exe"),
        (Join-Path $env:WINDIR "Microsoft.NET\Framework\v4.0.30319\csc.exe"),
        (Join-Path $env:WINDIR "Microsoft.NET\Framework64\v3.5\csc.exe"),
        (Join-Path $env:WINDIR "Microsoft.NET\Framework\v3.5\csc.exe"),
        "csc.exe"
    )
    $compiler = $null
    foreach ($candidate in $cscCandidates) {
        $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($cmd) {
            $compiler = $cmd.Source
            break
        }
    }
    if (-not $compiler) {
        throw "Unable to locate csc.exe on this system"
    }

    $compressionCoreRef = Find-FrameworkAssembly -AssemblyFileName "System.IO.Compression.dll"
    if (-not $compressionCoreRef) {
        $compressionCoreRef = Join-Path ${env:WINDIR} "Microsoft.NET\Framework64\v4.0.30319\System.IO.Compression.dll"
        if (-not (Test-Path $compressionCoreRef)) {
            $compressionCoreRef = Join-Path ${env:WINDIR} "Microsoft.NET\Framework\v4.0.30319\System.IO.Compression.dll"
        }
    }
    $compressionFsRef = Find-FrameworkAssembly -AssemblyFileName "System.IO.Compression.FileSystem.dll"
    if (-not $compressionFsRef) {
        $compressionFsRef = Join-Path ${env:WINDIR} "Microsoft.NET\Framework64\v4.0.30319\System.IO.Compression.FileSystem.dll"
        if (-not (Test-Path $compressionFsRef)) {
            $compressionFsRef = Join-Path ${env:WINDIR} "Microsoft.NET\Framework\v4.0.30319\System.IO.Compression.FileSystem.dll"
        }
    }
    if (-not (Test-Path $compressionCoreRef) -or -not (Test-Path $compressionFsRef)) {
        throw "Unable to locate compression assemblies for csc compilation"
    }

    Write-Host "Compiling embedded installer with csc..."
    & $compiler /nologo /target:winexe /optimize+ "/out:$resolvedOutputExe" "/resource:$zipPath,Payload.zip" "/reference:$compressionCoreRef" "/reference:$compressionFsRef" $stubPath
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $resolvedOutputExe)) {
        throw "Failed to compile installer executable"
    }

    Write-Host "Created installer: $resolvedOutputExe"
}
finally {
    if (Test-Path $tempDir) {
        Remove-Item -Recurse -Force $tempDir
    }
}
