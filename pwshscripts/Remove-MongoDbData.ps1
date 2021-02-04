Param(
    [Parameter(Mandatory = $true)] [string] $InstallPath
)

Remove-Item -Path "$InstallPath/var/data/db*/*", "$InstallPath/var/log/*" -Recurse -Force
