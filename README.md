# gpt.vxp

![image](https://github.com/raspiduino/gpt.vxp/assets/68118236/4a1a016f-4e1f-444b-a227-58467fa9a24f)

## What is this?
This is a ChatGPT client running on MRE platform.

## What does it run on?
[MRE platform](https://web.archive.org/web/20150922183623/http://mre.mediatek.com/), for example [Nokia S30+](https://en.wikipedia.org/wiki/Series_30%2B) phones like Nokia 220 (tested), Nokia 225 (tested),... and some other (mostly Chinese) phones. If your (cell / feature) phone can run `.vxp` apps, it's likely to be able to run this app.

## How does it work?
gpt.vxp is a ChatGPT client that uses the MRE editor UI for prompt entry and response history.

## How to use?
When you open the app, enter a prompt in the lower editor and choose **Send** to submit it. Responses and connection status are shown in the history editor above. The **History** softkey expands the response history; choose **Input** to return to prompt entry. **Clear** clears the displayed history.
<br>**Note:** The current MRE text conversion supports ASCII prompts, matching the previous console input behavior. You need to escape the string yourself (if you need to), since escape engine in this version haven't implemented.

Text entry uses the phone's MRE editor and its configured input method.

## Author
- [Ximik Boda](https://github.com/XimikBoda) for Terminal emulator (from TelnetVXP)
- [gvl610](https://github.com/raspiduino) for other stuffs
