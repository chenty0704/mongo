Param (
    [Parameter(Mandatory = $true)] [string] $UserName
)

# Start PowerShell upon login.
$homeDirectory = "/home/$UserName"
Copy-Item -Path "/etc/skel/.bashrc" -Destination $homeDirectory
Add-Content -Path "$homeDirectory/.bashrc" -Value "`n# Start PowerShell`npwsh"

# Enable PowerShell remoting.
$sshConfigPath = "/etc/ssh/sshd_config"
$sshConfig = [System.Collections.Generic.List[string]](Get-Content -Path $sshConfigPath)
$subSystemLine = $sshConfig.FindLastIndex( { Param($line) $line.StartsWith("Subsystem") })
$sshConfig.Insert($subSystemLine + 1, "Subsystem`tpowershell`t/snap/bin/pwsh -sshs -NoLogo")
$sshConfig | Out-File -FilePath $sshConfigPath -Force
service ssh restart
