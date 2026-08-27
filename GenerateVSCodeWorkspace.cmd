@echo off
setlocal EnableExtensions

rem Standalone Unreal Engine VS Code workspace generator.
rem Place this file next to a .uproject and double-click it.
rem Optional: GenerateVSCodeWorkspace.cmd "D:\Path\Game.uproject" -NoOpen

set "UE_VSCODE_TOOL_FILE=%~f0"
set "UE_VSCODE_TOOL_ARGS=%*"

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$raw=[IO.File]::ReadAllText($env:UE_VSCODE_TOOL_FILE); $marker='#==POWERSHELL_PAYLOAD==#'; $position=$raw.LastIndexOf($marker, [StringComparison]::Ordinal); if($position -lt 0){throw 'Embedded PowerShell payload was not found.'}; $payload=$raw.Substring($position + $marker.Length); & ([scriptblock]::Create($payload)) -ArgumentLine $env:UE_VSCODE_TOOL_ARGS"
set "TOOL_EXIT_CODE=%ERRORLEVEL%"

if not "%TOOL_EXIT_CODE%"=="0" (
    echo.
    echo Workspace generation failed. Review the error above.
    pause
)

exit /b %TOOL_EXIT_CODE%

#==POWERSHELL_PAYLOAD==#
param(
    [Parameter()]
    [string] $ArgumentLine
)

$ErrorActionPreference = 'Stop'

function Resolve-UnrealEngineRoot {
    param(
        [Parameter(Mandatory)]
        [string] $Association
    )

    if (Test-Path -LiteralPath $Association -PathType Container) {
        return (Resolve-Path -LiteralPath $Association).Path
    }

    $launcherKeys = @(
        "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Association",
        "HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$Association"
    )

    foreach ($key in $launcherKeys) {
        if (Test-Path -LiteralPath $key) {
            $installedDirectory = (Get-ItemProperty -LiteralPath $key -Name InstalledDirectory).InstalledDirectory
            if ($installedDirectory -and (Test-Path -LiteralPath $installedDirectory -PathType Container)) {
                return (Resolve-Path -LiteralPath $installedDirectory).Path
            }
        }
    }

    $sourceBuildsKey = 'HKCU:\Software\Epic Games\Unreal Engine\Builds'
    if (Test-Path -LiteralPath $sourceBuildsKey) {
        $sourceBuilds = Get-ItemProperty -LiteralPath $sourceBuildsKey
        $registeredBuild = $sourceBuilds.PSObject.Properties[$Association]
        if ($registeredBuild -and (Test-Path -LiteralPath $registeredBuild.Value -PathType Container)) {
            return (Resolve-Path -LiteralPath $registeredBuild.Value).Path
        }
    }

    $fallbacks = @()
    if ($env:ProgramFiles) {
        $fallbacks += Join-Path $env:ProgramFiles "Epic Games\UE_$Association"
    }
    if (${env:ProgramFiles(x86)}) {
        $fallbacks += Join-Path ${env:ProgramFiles(x86)} "Epic Games\UE_$Association"
    }

    foreach ($candidate in $fallbacks) {
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "Unable to locate Unreal Engine '$Association'. Check EngineAssociation in the .uproject file."
}

$noOpen = $ArgumentLine -match '(?i)(?:^|\s)-NoOpen(?:\s|$)'
$projectArgument = [regex]::Replace($ArgumentLine, '(?i)(?:^|\s)-NoOpen(?:\s|$)', ' ').Trim()
if ($projectArgument.StartsWith('"') -and $projectArgument.EndsWith('"')) {
    $projectArgument = $projectArgument.Substring(1, $projectArgument.Length - 2)
}

if ([string]::IsNullOrWhiteSpace($projectArgument)) {
    $toolDirectory = Split-Path -Parent $env:UE_VSCODE_TOOL_FILE
    $projects = @(Get-ChildItem -LiteralPath $toolDirectory -Filter '*.uproject' -File)
    if ($projects.Count -ne 1) {
        throw "Expected exactly one .uproject next to this tool, but found $($projects.Count). You can drag a .uproject onto the tool instead."
    }
    $project = $projects[0]
}
else {
    $project = Get-Item -LiteralPath $projectArgument
}

if ($project.Extension -ne '.uproject') {
    throw "The selected file is not a .uproject: '$($project.FullName)'"
}

$projectDescriptor = Get-Content -LiteralPath $project.FullName -Raw | ConvertFrom-Json
$engineAssociation = [string] $projectDescriptor.EngineAssociation
if ([string]::IsNullOrWhiteSpace($engineAssociation)) {
    throw "EngineAssociation is missing from '$($project.FullName)'."
}

$engineRoot = Resolve-UnrealEngineRoot -Association $engineAssociation
$unrealBuildToolCandidates = @(
    (Join-Path $engineRoot 'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe'),
    (Join-Path $engineRoot 'Engine\Binaries\DotNET\UnrealBuildTool.exe')
)
$unrealBuildTool = $unrealBuildToolCandidates | Where-Object {
    Test-Path -LiteralPath $_ -PathType Leaf
} | Select-Object -First 1
if (-not $unrealBuildTool) {
    throw "UnrealBuildTool was not found under '$engineRoot'."
}

$sourceDirectory = Join-Path $project.DirectoryName 'Source'
$editorTargets = @(Get-ChildItem -LiteralPath $sourceDirectory -Filter '*Editor.Target.cs' -File -Recurse)
$preferredTargetName = "$($project.BaseName)Editor.Target.cs"
$preferredTargets = @($editorTargets | Where-Object { $_.Name -eq $preferredTargetName })
if ($preferredTargets.Count -eq 1) {
    $editorTarget = $preferredTargets[0]
}
elseif ($editorTargets.Count -eq 1) {
    $editorTarget = $editorTargets[0]
}
else {
    throw "Unable to select an editor target. Found $($editorTargets.Count) *Editor.Target.cs files under '$sourceDirectory'."
}
$editorTargetName = $editorTarget.Name -replace '\.Target\.cs$', ''

$buildScript = Join-Path $engineRoot 'Engine\Build\BatchFiles\Build.bat'
$unrealEditor = Join-Path $engineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$visualizerFile = Join-Path $engineRoot 'Engine\Extras\VisualStudioDebugging\Unreal.natvis'
foreach ($requiredFile in @($buildScript, $unrealEditor)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required Unreal Engine file was not found: '$requiredFile'."
    }
}

$engineVersion = $null
$versionMatch = [regex]::Match($engineAssociation, '^(?<Major>\d+)\.(?<Minor>\d+)')
if ($versionMatch.Success) {
    $engineVersion = [version]::new(
        [int] $versionMatch.Groups['Major'].Value,
        [int] $versionMatch.Groups['Minor'].Value
    )
}

$visualStudioVersion = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
    $detectedVersion = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
    if ($LASTEXITCODE -eq 0 -and $detectedVersion) {
        $visualStudioVersion = [version] ($detectedVersion | Select-Object -Last 1)
    }
}

$compiler = 'VisualStudio2022'
if ($engineVersion -and $engineVersion -ge [version]::new(5, 8) -and
    $visualStudioVersion -and $visualStudioVersion.Major -ge 18) {
    $compiler = 'VisualStudio2026'
}

$vscodeDirectory = Join-Path $project.DirectoryName '.vscode'
if (-not (Test-Path -LiteralPath $vscodeDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $vscodeDirectory | Out-Null
}

Write-Host "Project: $($project.FullName)"
Write-Host "Engine:  $engineRoot"
Write-Host "Target:  $editorTargetName"
Write-Host 'Generating IntelliSense compile database...'

& $unrealBuildTool `
    -Mode=GenerateClangDatabase `
    $editorTargetName `
    Win64 `
    Development `
    "-Project=$($project.FullName)" `
    "-OutputDir=$vscodeDirectory" `
    -NoExecCodeGenActions `
    "-Compiler=$compiler"

$compileDatabasePath = Join-Path $vscodeDirectory 'compile_commands.json'
$compileDatabaseAvailable = $LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $compileDatabasePath -PathType Leaf)
if (-not $compileDatabaseAvailable) {
    Write-Warning 'The IntelliSense database could not be generated. Build and launch tasks will still be created.'
}

$workspacePath = Join-Path $project.DirectoryName "$($project.BaseName).code-workspace"
$buildTaskName = "Build $editorTargetName (Development Win64)"
$settings = [ordered] @{
    'C_Cpp.intelliSenseEngine' = 'default'
    'files.exclude' = [ordered] @{
        '**/.git' = $true
        '**/Binaries' = $true
        '**/DerivedDataCache' = $true
        '**/Intermediate' = $true
        '**/Saved' = $true
    }
    'search.exclude' = [ordered] @{
        '**/Binaries' = $true
        '**/DerivedDataCache' = $true
        '**/Intermediate' = $true
        '**/Saved' = $true
    }
}
if ($compileDatabaseAvailable) {
    $settings['C_Cpp.default.compileCommands'] = '${workspaceFolder}/.vscode/compile_commands.json'
}

$tasks = [ordered] @{
    version = '2.0.0'
    tasks = @(
        [ordered] @{
            label = $buildTaskName
            type = 'process'
            command = $buildScript
            args = @(
                $editorTargetName,
                'Win64',
                'Development',
                "-Project=$($project.FullName)",
                '-WaitMutex',
                '-FromMsBuild'
            )
            options = [ordered] @{
                cwd = $project.DirectoryName
            }
            problemMatcher = @('$msCompile')
            group = [ordered] @{
                kind = 'build'
                isDefault = $true
            }
        }
    )
}

$launch = [ordered] @{
    version = '0.2.0'
    configurations = @(
        [ordered] @{
            name = "Launch $($project.BaseName) Editor (Build + Debug)"
            type = 'cppvsdbg'
            request = 'launch'
            program = $unrealEditor
            args = @($project.FullName, '-skipcompile')
            cwd = $project.DirectoryName
            stopAtEntry = $false
            console = 'integratedTerminal'
            visualizerFile = $visualizerFile
            preLaunchTask = $buildTaskName
        },
        [ordered] @{
            name = 'Attach to UnrealEditor'
            type = 'cppvsdbg'
            request = 'attach'
            processId = '${command:pickProcess}'
            visualizerFile = $visualizerFile
        }
    )
}

$extensions = [ordered] @{
    recommendations = @(
        'ms-vscode.cpptools',
        'ms-dotnettools.csharp'
    )
}

$workspace = [ordered] @{
    folders = @(
        [ordered] @{
            name = $project.BaseName
            path = '.'
        }
    )
}

function Write-JsonFile {
    param(
        [Parameter(Mandatory)]
        [string] $Path,

        [Parameter(Mandatory)]
        $Value
    )

    $json = $Value | ConvertTo-Json -Depth 10
    [IO.File]::WriteAllText(
        $Path,
        $json + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false)
    )
}

Write-JsonFile -Path (Join-Path $vscodeDirectory 'tasks.json') -Value $tasks
Write-JsonFile -Path (Join-Path $vscodeDirectory 'launch.json') -Value $launch
Write-JsonFile -Path (Join-Path $vscodeDirectory 'settings.json') -Value $settings
Write-JsonFile -Path (Join-Path $vscodeDirectory 'extensions.json') -Value $extensions
Write-JsonFile -Path $workspacePath -Value $workspace

Write-Host "Workspace generated: $workspacePath" -ForegroundColor Green
Write-Host "VS Code launch config: $(Join-Path $vscodeDirectory 'launch.json')" -ForegroundColor Green

if (-not $noOpen) {
    $codeCommand = Get-Command 'code.cmd' -ErrorAction SilentlyContinue
    if (-not $codeCommand) {
        $commonCodeCommand = Join-Path $env:LOCALAPPDATA 'Programs\Microsoft VS Code\bin\code.cmd'
        if (Test-Path -LiteralPath $commonCodeCommand -PathType Leaf) {
            $codeCommand = Get-Item -LiteralPath $commonCodeCommand
        }
    }

    if ($codeCommand) {
        Write-Host 'Opening workspace in Visual Studio Code...'
        & $codeCommand.Source $workspacePath
    }
    else {
        Write-Warning "VS Code command 'code' was not found. Open this file manually: $workspacePath"
    }
}
