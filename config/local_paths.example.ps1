# Copy this file to local_paths.ps1 and update it for the current computer.
# local_paths.ps1 is machine-specific and intentionally excluded from Git.
$EchoLocalPaths = @{
    SdkRoot      = "D:\sftoware\TI_CCS\mspm0_sdk_2_10_00_04"
    KeilUv4      = "D:\keil mdk\UV4\UV4.exe"
    SysConfigCli = "C:\ti\sysconfig_1.26.0\sysconfig_cli.bat"
    OpenOcdRoot  = "D:\sftoware\openOCD\xpack-openocd-0.12.0-7-win32-x64\xpack-openocd-0.12.0-7"
    GnuArmBin    = "D:\sftoware\cube CLT\STM32CubeCLT_1.18.0\GNU-tools-for-STM32\bin"
    ArmClangBin  = "D:\keil mdk\ARM\ARMCLANG\bin"
}
