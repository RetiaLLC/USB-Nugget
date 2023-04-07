# **USB Nugget**: Cat-Themed USB Attacks
A versatile USB attack platform that lets you hack computers in seconds using a [USB Nugget](https://usbnugget.com)!  

<img src="images/USB-Nugget.png"/>

## Resources:
- [USB Nugget Payloads](https://github.com/HakCat-Tech/USB-Nugget-Payloads)  

## How to Update your Nugget:
1.	[Download the latest binary file here](https://github.com/HakCat-Tech/USB-Nugget/releases/)
2.	Place your Nugget in [Device Firmware Upgrade (DFU) mode]().
4.	Open our [web flasher tool](https://hakcat-tech.github.io/esp-web-flasher/) in Google Chrome (other browsers not currently supported)
5.	Click on "Connect" and then select the "ESP32-S2" board. Click "Erase" and "OK" to continue.
6.	Once you see "Finished", click "Choose a file" and select the .BIN file you downloaded in step one. Click "Program" to flash your Nugget!
7.	When its done, unplug your Nugget and plug it in again to see the new features. 

## Creating Payloads
To upload a payload, you can save a `.txt` file to the USB Nugget flash drive.  Payloads must be saved under an operating system, then category type. (OS->Category->Payload.txt)
## Accessing the Web Interface

You can create, edit and deploy payloads from the web interface.

To access the web interface, connect to `Nugget AP` with the password `nugget123`.  In a web browser, navigate to `192.168.4.1` to access the payload deployment system.

## Updating AP Credentials & Keyboard ID
To edit your USB Nugget's default AP name & password, edit or create the `.usbnugget.conf` file on your NUGGET drive, and add the following 2 lines:
```
network = "Nugget AP"
password = "nugget123"
```
To change the VID and PID of the keyboard, you can just add:
```
vid = "0x05ac"
pid = "0x20b"
```

If `.usbnugget.conf` is not present or contains invalid entries, the above
settings will be used.

## Nugget Scripting

Use `TYPE` to type something in. The following script types "Hello, world!"
```
TYPE Hello, world!
```

You can use modifier keys such as `CONTROL`/`CTRL`, `ALT`, `ENTER`, `COMMAND`, etc.
```
CONTROL t
ALT SHIFT n
```

Use `WAIT` when you need to wait for something to happen.
```
CMD SPACE
TYPE firefox
// wait 2000 milliseconds (one second) for firefox to open
WAIT 2000
...
```

The `SCREEN` command outputs text to the screen.
```
SCREEN this is some information
DELAY 1000
```

The `LED` command changes the color of the NeoPixel LED. Use with one of the following color options
```
// RED
LED R

// GREEN
LED G

// BLUE
LED B

// CYAN
LED C

// YELLOW
LED Y

// MAROON
LED M

// WHITE
LED W
```

You can use `LOCALE` to change the active keyboard layout. Currently, English (`EN`), Dutch(`DE`), Spanish (`ES`), French (`FR`), and Portuguese (`PT`) are supported.
```
LOCALE ES
```
