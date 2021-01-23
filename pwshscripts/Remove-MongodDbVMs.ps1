Param(
    [Parameter(Mandatory = $true)] [int] $NumVMs,
    [string] $ResourceGroupName = "rg-mongodb",
    [string] $Location = "eastus"
)

Clear-Host

for ($i = 0; $i -lt $NumVMs; $i++) {
    $virtualMachineName = "vmmongodb$i"

    Remove-AzVM -Name $virtualMachineName -ResourceGroupName $ResourceGroupName -Force
    Remove-AzDisk -Name "osdisk-$virtualMachineName" -ResourceGroupName $ResourceGroupName -Force
    Remove-AzNetworkInterface -Name "nic-$virtualMachineName" -ResourceGroupName $ResourceGroupName -Force
    Remove-AzPublicIpAddress -Name "pip-$virtualMachineName-$Location" -ResourceGroupName $ResourceGroupName -Force
}
