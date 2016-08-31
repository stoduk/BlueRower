# BlueRower
I bought a [Water Rower](http://www.waterrower.com]) rowing machine many years ago to train for a charity cycle ride.  It is a great machine in many ways, ideal for home use due to it being quiet during use and the timber frame makes it look a more natural fit for the home.

My model has the Series IV Performance Monitor - basically a small computer that can tell you the usual stats on how you are performing, as well as reading data from the integrated Polar heart rate monitor.  However, the only option for storing this data is to connect the device via a propriatary cable (standard DB9 connector on computer end, a DIN connector on the rowing machine end).  My main computer at home is a Mac, so it is very hard to find a decent DB9:USB adaptor (many just don't work, some require installing kernal drivers that are too flaky to want to use).  Wireless would be ideal but there aren't any standard options for this.

## Aims
So this project meets the following aims:
* allow data to be saved on a computer without requiring cables
* allow data to be stored on the device for later replay (to cope with cases where the computer isn't available)
* display key data on a graphical LCD on the device itself (as the computer won't always be in a position where I can read the screen)
* ideally also upload the resultant data to [Run Keeper](http://www.runkeeper.com) or similar

## Features
* Streaming of rowing machine output over bluetooth serial connection to any computer
* Storing of sessions locally (on micro SD), either to be streamed over bluetooth later or by copying from the micro SD card
 * All received data is timestamped for this purpose so it can be played back in "real time", which is required for this but also useful for testing!
* Graphical display shows all the key data that it is sending to the computer or storing

## Status
The project is working fully, though there is plenty of improvements that could be made!  Using it more would be the biggest single improvement I could make :)

I have various python scripts to handle receiving the bluetooth data stream for live sessions and replayed sessions, as well as updating the clock on the device and testing the various modules.  I also have scripts to create data for upload to RunKeeper (requires a faked .gpx GPS data file amongst other things).

Update: actually things are totally broken at the moment :(  I had to borrow the Arduino and Bluetooth modules for another project and, thanks to a dodgy power supply module from China, those parts are now dead.  Replacements are ordered so hopefully I'll be back to rowing soon!
## Future work
* The built in graphical display could do with some more work to display more useful data
* Replacing the old bluetooth module with a BTLE module (like an ''Arduino Primo: hint, hint!'') would let me replace the small graphical display with an IOS device to show the data as well as instant uploading to Runkeeper and perhaps integration with Apple's HealthKit.
* It would be preferable to design and build a real PCB (both for asthetics and reliability), rather than the current protoboard setup.
* An enclosure would go a long way to make it look better, and perhaps a way to mount it next to the existing monitor
* I could potentially remove the Series IV Performance Monitor and handle the raw data myself (from rowing and heart rate sensors), removing one redundant system

## Photos
I don't have any photos of the fully working system, unfortunately.  I do have a photo of the bring up work for the graphical display, and a photo of the device powered off.

![some words](https://raw.githubusercontent.com/stoduk/BlueRower/master/IMG_1766.JPG)
![some words](https://raw.githubusercontent.com/stoduk/BlueRower/master/IMG_3628.JPG)
![some words](https://raw.githubusercontent.com/stoduk/BlueRower/master/IMG_3630.JPG)