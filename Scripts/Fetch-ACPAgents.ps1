# Downloads every agent in the ACP registry into ThirdParty/ACPAgents so the plugin
# can install them without touching the network. Nothing downloaded is executed:
# npm runs with --ignore-scripts and Python packages are installed from wheels only.
# Re-running skips agents whose bundle.json already matches the registry version.
#
#   powershell -ExecutionPolicy Bypass -File Scripts\Fetch-ACPAgents.ps1 [-Only id1,id2] [-Force]
#
# -RunInstallScripts downloads nothing new: it runs the npm install scripts that the
# download skipped, which does execute third-party code, for agents already fetched.
param(
	[string[]]$Only,
	[switch]$Force,
	[switch]$RunInstallScripts,
	[double]$MinFreeGB = 3
)

$ErrorActionPreference = 'Stop'
$Platform    = 'windows-x86_64'
$RegistryUrl = 'https://cdn.agentclientprotocol.com/registry/v1/latest/registry.json'
$Root        = Join-Path (Split-Path $PSScriptRoot -Parent) 'ThirdParty\ACPAgents'
$LogDir      = Join-Path $Root '_logs'
$Tar         = Join-Path $env:SystemRoot 'System32\tar.exe'
$Curl        = Join-Path $env:SystemRoot 'System32\curl.exe'
$TimeoutMs   = 15 * 60 * 1000

New-Item -ItemType Directory -Force $LogDir | Out-Null

function Invoke-Tool([string]$Exe, [string]$ArgLine, [string]$LogName)
{
	$Out = Join-Path $LogDir "$LogName.out.txt"
	$Err = Join-Path $LogDir "$LogName.err.txt"
	$P = Start-Process -FilePath $Exe -ArgumentList $ArgLine -NoNewWindow -PassThru `
		-RedirectStandardOutput $Out -RedirectStandardError $Err
	$null = $P.Handle
	if (-not $P.WaitForExit($TimeoutMs))
	{
		& taskkill.exe /PID $P.Id /T /F 2>$null | Out-Null
		return -1
	}
	return $P.ExitCode
}

function Save-Bundle([string]$Dir, $Agent, [string]$Kind, [string]$Runner, [string]$Entrypoint, $CmdArgs, $Skipped)
{
	$Body = [ordered]@{
		agentId    = $Agent.id
		version    = $Agent.version
		kind       = $Kind
		runner     = $Runner
		entrypoint = $Entrypoint.Replace('\', '/')
		args       = @($CmdArgs | Where-Object { $_ -ne $null })
		skippedInstallScripts = @($Skipped | Where-Object { $_ -ne $null })
	}
	[IO.File]::WriteAllText((Join-Path $Dir 'bundle.json'), ($Body | ConvertTo-Json -Depth 4))
}

function Install-Binary($Agent, $Bin, [string]$Dir)
{
	$Name = [Uri]::UnescapeDataString(([Uri]$Bin.archive).Segments[-1])
	$Archive = Join-Path $Dir $Name
	$Code = Invoke-Tool $Curl "-fsSL --retry 3 --retry-all-errors -o `"$Archive`" `"$($Bin.archive)`"" "$($Agent.id).download"
	if ($Code -ne 0) { throw "download failed (curl exit $Code)" }

	$Rel = ($Bin.cmd -replace '^\./', '').Replace('\', '/')
	if ($Name -notmatch '\.exe$')
	{
		# tar reports symlinks it can't create on Windows as errors; only the entrypoint decides.
		$Code = Invoke-Tool $Tar "-xf `"$Archive`" -C `"$Dir`"" "$($Agent.id).extract"
		Remove-Item -LiteralPath $Archive -Force
		if (-not (Test-Path -LiteralPath (Join-Path $Dir $Rel))) { throw "entrypoint $Rel missing after extract (tar exit $Code)" }
	}
	elseif (-not (Test-Path -LiteralPath (Join-Path $Dir $Rel))) { throw "entrypoint $Rel missing" }

	Save-Bundle $Dir $Agent 'binary' 'direct' $Rel $Bin.args @()
}

function Get-InstallScriptPackages([string]$NodeModules)
{
	Get-ChildItem -LiteralPath $NodeModules -Recurse -Filter package.json -File -ErrorAction SilentlyContinue |
		Select-String -Pattern '"(preinstall|install|postinstall)"\s*:' -List -ErrorAction SilentlyContinue |
		ForEach-Object { (Split-Path $_.Path -Parent).Substring($NodeModules.Length + 1).Replace('\', '/') }
}

function Install-Npx($Agent, $Npx, [string]$Dir)
{
	$Pkg = "{`"name`":`"uecp-acp-$($Agent.id)`",`"version`":`"0.0.0`",`"private`":true}"
	[IO.File]::WriteAllText((Join-Path $Dir 'package.json'), $Pkg)

	$Code = Invoke-Tool 'npm.cmd' "install --prefix `"$Dir`" --ignore-scripts --no-save --no-fund --no-audit --loglevel=error $($Npx.package)" "$($Agent.id).npm"
	if ($Code -ne 0) { throw "npm install failed (exit $Code)" }

	$PkgName = $Npx.package -replace '(?<=.)@[^@/]+$', ''
	$PkgDir  = Join-Path $Dir "node_modules\$PkgName"
	$Json    = Get-Content -LiteralPath (Join-Path $PkgDir 'package.json') -Raw | ConvertFrom-Json
	$BinRel  = $null
	if ($Json.bin -is [string]) { $BinRel = $Json.bin }
	elseif ($Json.bin) { $BinRel = @($Json.bin.PSObject.Properties)[0].Value }
	if (-not $BinRel) { $BinRel = $Json.main }
	if (-not $BinRel) { throw "package has no bin or main" }
	$BinRel = $BinRel -replace '^(\./)+', ''

	$BinAbs = Join-Path $PkgDir $BinRel
	if (-not (Test-Path -LiteralPath $BinAbs)) { throw "bin $BinRel missing (likely created by a skipped install script)" }

	$Skipped = @(Get-InstallScriptPackages (Join-Path $Dir 'node_modules'))
	Save-Bundle $Dir $Agent 'npx' 'node' "node_modules/$PkgName/$BinRel" $Npx.args $Skipped
}

function Install-Uvx($Agent, $Uvx, [string]$Dir)
{
	$env:UV_PYTHON_INSTALL_DIR = Join-Path $Dir 'python'
	$Venv = Join-Path $Dir 'venv'
	$Installed = $false
	# uvx accepts name@version; pip only takes name==version.
	$Spec = $Uvx.package -replace '(?<=.)@(?=[^@/]+$)', '=='
	foreach ($Py in '3.13', '3.12', '3.11')
	{
		if (Test-Path -LiteralPath $Venv) { Remove-Item -LiteralPath $Venv -Recurse -Force }
		$Code = Invoke-Tool 'uv' "venv `"$Venv`" --python $Py --managed-python --relocatable" "$($Agent.id).venv$Py"
		if ($Code -ne 0) { continue }
		$Code = Invoke-Tool 'uv' "pip install --python `"$Venv\Scripts\python.exe`" --only-binary :all: `"$Spec`"" "$($Agent.id).pip$Py"
		if ($Code -eq 0) { $Installed = $true; break }
	}
	if (-not $Installed) { throw "uv pip install failed on Python 3.11-3.13 (see _logs)" }

	$ExeName = ($Uvx.package -replace '[=<>!~@].*$', '') + '.exe'
	$Rel = "venv/Scripts/$ExeName"
	if (-not (Test-Path -LiteralPath (Join-Path $Dir $Rel))) { throw "entrypoint $Rel missing" }
	Save-Bundle $Dir $Agent 'uvx' 'direct' $Rel $Uvx.args @()
}

if ($RunInstallScripts)
{
	foreach ($Dir in Get-ChildItem -LiteralPath $Root -Directory | Where-Object { $_.Name -ne '_logs' })
	{
		$BundlePath = Join-Path $Dir.FullName 'bundle.json'
		if (-not (Test-Path -LiteralPath $BundlePath)) { continue }
		if ($Only -and $Only -notcontains $Dir.Name) { continue }
		$Bundle = Get-Content -LiteralPath $BundlePath -Raw | ConvertFrom-Json
		if (@($Bundle.skippedInstallScripts).Count -eq 0) { continue }

		$Code = Invoke-Tool 'npm.cmd' "rebuild --prefix `"$($Dir.FullName)`" --foreground-scripts --no-fund --no-audit" "$($Dir.Name).rebuild"
		if ($Code -eq 0)
		{
			$Bundle.skippedInstallScripts = @()
			[IO.File]::WriteAllText($BundlePath, ($Bundle | ConvertTo-Json -Depth 4))
			"OK    $($Dir.Name)"
		}
		else { "FAIL  $($Dir.Name): npm rebuild exit $Code (see _logs)" }
	}
	return
}

$RegistryPath = Join-Path $Root 'registry.json'
& $Curl -fsSL --retry 3 -o $RegistryPath $RegistryUrl
if ($LASTEXITCODE -ne 0) { throw "registry download failed (curl exit $LASTEXITCODE)" }
$Agents = (Get-Content -LiteralPath $RegistryPath -Raw | ConvertFrom-Json).agents
if ($Only) { $Agents = $Agents | Where-Object { $Only -contains $_.id } }

$Results = @()
foreach ($Agent in $Agents)
{
	$Dir = Join-Path $Root $Agent.id
	$BundlePath = Join-Path $Dir 'bundle.json'
	if (-not $Force -and (Test-Path -LiteralPath $BundlePath) -and
		(Get-Content -LiteralPath $BundlePath -Raw | ConvertFrom-Json).version -eq $Agent.version)
	{
		"SKIP  $($Agent.id) $($Agent.version)"
		$Results += [pscustomobject]@{ id = $Agent.id; version = $Agent.version; status = 'ok'; error = '' }
		continue
	}

	$FreeGB = (Get-PSDrive ($Root.Substring(0, 1))).Free / 1GB
	if ($FreeGB -lt $MinFreeGB)
	{
		"STOP  only $([math]::Round($FreeGB, 1)) GB free, below the $MinFreeGB GB floor"
		$Results += [pscustomobject]@{ id = $Agent.id; version = $Agent.version; status = 'not-attempted'; error = 'low disk' }
		continue
	}

	if (Test-Path -LiteralPath $Dir) { & cmd.exe /c rd /s /q "$Dir" }
	New-Item -ItemType Directory -Force $Dir | Out-Null
	try
	{
		$Dist = $Agent.distribution
		$Bin  = if ($Dist.binary) { $Dist.binary.$Platform } else { $null }
		if ($Bin) { Install-Binary $Agent $Bin $Dir }
		elseif ($Dist.npx) { Install-Npx $Agent $Dist.npx $Dir }
		elseif ($Dist.uvx) { Install-Uvx $Agent $Dist.uvx $Dir }
		else { throw "no distribution for $Platform" }

		$MB = [math]::Round((Get-ChildItem -LiteralPath $Dir -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum / 1MB)
		"OK    $($Agent.id) $($Agent.version) ($MB MB)"
		$Results += [pscustomobject]@{ id = $Agent.id; version = $Agent.version; status = 'ok'; error = '' }
	}
	catch
	{
		"FAIL  $($Agent.id) $($Agent.version): $($_.Exception.Message)"
		$Results += [pscustomobject]@{ id = $Agent.id; version = $Agent.version; status = 'failed'; error = $_.Exception.Message }
	}
}

[IO.File]::WriteAllText((Join-Path $Root '_report.json'), (ConvertTo-Json @($Results) -Depth 3))
$Failed = @($Results | Where-Object { $_.status -ne 'ok' })
"DONE  $($Results.Count - $Failed.Count)/$($Results.Count) ok"
