# Cavli QM Serial Linux Kernel Module User Manual

## Ensure that the Secure boot setting of the device (i.e., PC) has been Disabled. Else this process will throw ERROR

## Compile

```
$ cd cavli-qm-serial
$ make
```
## Finding the value of (uname -r)
```
$ sudo uname -r
```
## Install Module  -- Insert the value of (uname -r) in the line
    
```
$ sudo insmod /lib/modules/$(uname -r)/kernel/drivers/usb/serial/usbserial.ko
$ sudo insmod CavQMSerial_mod.ko
```

If module is installed successfully, this ```dmesg``` log should show

```
[14223.304548] Cavli 1-11.2:1.2: CavSerial converter detected
[14223.304685] usb 1-11.2: CavSerial converter now attached to ttyUSB0
[14223.309911] Cavli 1-11.2:1.3: CavSerial converter detected
[14223.310034] usb 1-11.2: CavSerial converter now attached to ttyUSB1
[14223.310050] Cavli QM Serial: 1.0.0.0
```
With the logs above, ```ttyUSB0``` is AT port and ```ttyUSB1``` is GNSS port  (as per the dmesg logs - might vary acccording to the device)
