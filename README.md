# gpt.vxp

![image](https://github.com/raspiduino/gpt.vxp/assets/68118236/4a1a016f-4e1f-444b-a227-58467fa9a24f)

![alt text](https://rdzdx.github.io/gpt.vxp/picture.jpg)

## File
- [gpt_gui_vxp.zip](https://rdzdx.github.io/gpt.vxp/gpt_gui_vxp.zip)

## Nokia Phone Signing

For use on Nokia mobile phones, the application must be signed using the IMSI code of your SIM card.
More information: https://vxpatch.luxferre.top

## What is this?
This is a ChatGPT client running on MRE platform.

## What does it run on?
[MRE platform](https://web.archive.org/web/20150922183623/http://mre.mediatek.com/), for example [Nokia S30+](https://en.wikipedia.org/wiki/Series_30%2B) phones like Nokia 220 (tested), Nokia 225 (tested),... and some other (mostly Chinese) phones. If your (cell / feature) phone can run `.vxp` apps, it's likely to be able to run this app.

## How does it work?
gpt.vxp is a ChatGPT client that launches directly into an MRE editor UI for prompt entry and response history.

## How to use?
For using app you must initialy have ChatGPT API KEY. The **History** softkey expands the response history; choose **Back** to return to prompt entry.
Text entry uses the phone's MRE editor and its configured input method.

## HTTPS security
HTTPS certificate verification is disabled unconditionally. Connections remain encrypted, but the app does not authenticate the server, so a man-in-the-middle can intercept or alter traffic. Do not send sensitive information over an untrusted network.

## Author
- [Ximik Boda](https://github.com/XimikBoda) for Terminal emulator (from TelnetVXP)
- [gvl610](https://github.com/raspiduino) for other stuffs
