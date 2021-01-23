Param(
    [Parameter(Mandatory = $true)] [int] $NumVMs,
    [string] $ResourceGroupName = "rg-mongodb",
    [string] $Location = "eastus",
    [Microsoft.Azure.Commands.Network.Models.PSVirtualNetwork]
    $VirtualNetwork = $(Get-AzVirtualNetwork -Name "vnet-eastus" -ResourceGroupName "rg-vnet"),
    [Microsoft.Azure.Commands.Network.Models.PSSubnet]
    $Subnet = $(Get-AzVirtualNetworkSubnetConfig -Name "snet-eastus-001" -VirtualNetwork $VirtualNetwork),
    [Microsoft.Azure.Commands.Network.Models.PSNetworkSecurityGroup]
    $NetworkSecurityGroup = $(Get-AzNetworkSecurityGroup -Name "nsg-mongodballow" -ResourceGroupName "rg-vnet"),
    [string] $VirtualMachineSize = "Standard_D4s_v4",
    [int] $OSDiskSizeInGB = 64,
    [System.Management.Automation.PSCredential]
    $Credential = $(Get-Credential -Title "Administrator account"),
    [Parameter(Mandatory = $true)] [string] $SshPublicKey
)

Set-Item -Path Env:\SuppressAzurePowerShellBreakingChangeWarnings -Value "true"

Clear-Host

for ($i = 0; $i -lt $NumVMs; $i++) {
    $virtualMachineName = "vmmongodb$i"

    # Create the public IP address.
    $publicIpAddress = New-AzPublicIpAddress `
        -Name "pip-$virtualMachineName-$Location" `
        -ResourceGroupName $ResourceGroupName `
        -Location $Location `
        -Sku "Basic" `
        -AllocationMethod "Static" `
        -DomainNameLabel $virtualMachineName
    
    # Create the network interface.
    $ipConfig = New-AzNetworkInterfaceIpConfig `
        -Name "ipconfig" `
        -Primary `
        -Subnet $Subnet `
        -PublicIpAddress $publicIpAddress
    $networkInterface = New-AzNetworkInterface `
        -Name "nic-$virtualMachineName" `
        -ResourceGroupName $ResourceGroupName `
        -Location $Location `
        -IpConfiguration $ipConfig `
        -NetworkSecurityGroup $NetworkSecurityGroup `
        -EnableAcceleratedNetworking

    # Create the custom data.
    $indentedSetupScript = Get-Content -Path Setup-MongoDbVM.ps1 | ForEach-Object -Process { "      " + $_ } | Out-String
    $customData = (Get-Content -Path vmmongodb-cloud-init.yaml -Raw) -f "|`n$indentedSetupScript", $Credential.UserName

    # Create the virtual machine.
    $vmConfig = New-AzVMConfig `
        -VMName $virtualMachineName `
        -VMSize $VirtualMachineSize `
        -Priority "Spot" `
        -EvictionPolicy "Deallocate" | `
        Set-AzVMOperatingSystem `
        -Linux `
        -ComputerName $virtualMachineName `
        -Credential $Credential `
        -DisablePasswordAuthentication `
        -CustomData $customData | `
        Add-AzVMSshPublicKey `
        -KeyData $SshPublicKey `
        -Path "/home/$($Credential.UserName)/.ssh/authorized_keys" | `
        Set-AzVMSourceImage `
        -PublisherName "Canonical" `
        -Offer "0001-com-ubuntu-server-hirsute-daily" `
        -Skus "21_04-daily-gen2" `
        -Version "latest" | `
        Set-AzVMOSDisk `
        -Name "osdisk-$virtualMachineName" `
        -CreateOption "FromImage" `
        -DiskSizeInGB $OSDiskSizeInGB `
        -StorageAccountType "Premium_LRS" | `
        Add-AzVMNetworkInterface -NetworkInterface $networkInterface | `
        Set-AzVMBootDiagnostic -Disable
    New-AzVM `
        -ResourceGroupName $ResourceGroupName `
        -Location $Location `
        -VM $vmConfig
}
