# Unreal Engine HoloLens 2

From this repository you can compile a version of Unreal Engine that supports the Microsoft HoloLens 2.

## Release

HoloLens 2 Early Access (GitHub)

## Getting Started

These instructions have setup and configuration information specific to the HoloLens 2.

For detailed information on building and configuring Unreal Engine, see the information provided on the main Unreal Engine GitHub [repository](https://github.com/EpicGames/UnrealEngine).

### Prerequisites

* Microsoft HoloLens 2 HMD
* Windows Insider Account
* Microsoft Windows 10 Insider Preview Build (version 18317 or later)
* Windows Insider Preview SDK (version 190501 or later)
* Visual Studio 2017 (with current updates)

### Installing the Windows 10 Insider Preview Build, Windows SDK, and Visual Studio 2017

#### Windows 10 Insider Preview Build

1. Create a [Windows Insider account](https://insider.windows.com/en-us/).

1. Go to **Settings** > **Update & Security** > **Windows Insider Program** and click **Get Started** to access the latest build.

1. Once you are in **Settings**:
  * Enter the account you used to sign into the Windows Insider Program.
  * Follow the prompts to install.
  * Go to Settings > Update & Security > Windows Update and click Check for updates to install.

#### Windows Insider Preview SDK

1. Once you have installed the Windows 10 Insider Preview Build, install the pre-release version of the Windows 10 Software Development Kit (SDK).

  * [Download Windows SDK Insider Preview](https://www.microsoft.com/en-us/software-download/windowsinsiderpreviewSDK)

#### Visual Studio 2017
1. If you have not already installed and updated Visual Studio 2017, install latest version of Visual Studio 2017.

  * [Download Windows SDK Insider Preview](https://www.microsoft.com/en-us/software-download/windowsinsiderpreviewSDK) (Visual Studio is an optional download).

  
### Unreal Engine Project Setup
#### Project Creation
1. From the Unreal Engine Github site, make sure that you have downloaded and built Unreal Engine version 4.23.

2. In the **Unreal Project Browser**, go to the **New Project** section and create a new project targeting **Mobile/Tablet** and **Scalable 3D or 2D**.

3. Set the location where the project will be created and give the project a name.

4. Finally, press the **Create Project** button that is located in the lower right-hand corner to create the project.

#### Project Setup
1. Once the Editor has loaded, from the Main Toolbar, open the **Edit** menu and select **Plugins**.

2. Inside of the **Plugins** tab, and make sure that the **Mixed Reality** and **HoloLens** plugins are enabled.

3. Back on the Main Toolbar, select the **Edit** menu and select **Project Settings**.

4. From the **Project Settings** menu, under the **Platforms - HoloLens** section, scroll down and generate a **Certificate**.

  * Note: You can leave the form blank, if no security is required.

5. From the **Project Settings** menu, under the **Engine - Rendering** section, make sure that **Mobile HDR** is enabled.

6. If you see the **Restart Editor** button under the **Pending Changes** section, make sure to press it to restart the Editor, so that any changes that were made will be applied.

#### Project Packaging
In this section we will go over what you have to do to package your UE4 project so that it will work with a HoloLens 2 device.

1. To start the packing process, go to the Main Toolbar and from the **File** menu go to **Package Project** and select **HoloLens**.

2. Install the app through the **Windows Device Portal**.

#### Application Installation
1. Connect to the **Windows Device Portal**.
 * Obtain the **Device IP Address**.
    * **Settings** -> **Network** -> **Hardware Properties** (at the very bottom)
 * Open a web browser and enter the IP into address bar.
    * Note: The password to connect should be the default admin password.
 * Optional: To use a USB connection, follow the directions provided by Microsoft [here](https://docs.microsoft.com/en-us/windows/mixed-reality/using-visual-studio).
2. In the **Windows Device Portal** select **System** -> **Apps** from the left toolbar.
3. In the **Deploy Apps** section, check the **Allow me to select optional packages** box.
4. Click the **Choose file** button.
5. Browse to location of project, for example: **\Documents\Unreal Projects\ProjectName\Hololens**.
6. Select the **.appxbundle** file, and then press **Next**.
7. Choose **File** -> **Select the .appx file** -> **Press Install**.
8. Wait for the app to finish installing.
9. When the app installation is complete, you should see the message **Installation Complete: Package Successfully Registered**.
10. The **App** should then appear in the app list for the device.

### Installing the Holographic Remoting Player
  The Holographic Remoting Player is a companion app that connects to PC apps and games that support Holographic Remoting. Holographic Remoting streams holographic content from a PC to your Microsoft HoloLens in real-time, using a Wi-Fi connection.

  After setting up the device, download and install the Holographic Remoting Player using the below link.

  [Holographic Remoting Player](https://www.microsoft.com/en-us/p/holographic-remoting-player/9nblggh4sv40?lc=1033&activetab=pivot%3Aoverviewtab)

### HoloLens 2 General Information

  #### Operation

  * **Powering on the device:** Press and hold the power button for ~3 seconds until you see the LEDs light up in the following sequence (where **0** means a lit LED and **x** means an unlit LED):
    * OxOxO
    * xOxOx
    * OOOOO

  * **Going into standby mode:** Press and immediately release the power button - do NOT hold it down.
  It may appear that nothing is happening at first, but wait ~15 seconds. In standby mode, the LEDs will all turn off and the displays will go dark

  * **Forcing a full shutdown:** Press and hold the power button for ~5 seconds. The displays should say **goodbye** and you should hear a **bing bong** sound. When shutdown, all LEDs should turn off.

  * **Using Gestures:** During normal operation, the regular HL1 gestures and voice commands all work. Use the **bloom** gesture to bring up or close the main system menu. Use the **air tap** gesture to select items. You should see a rough hand mesh that follows your hand along with a **laser pointer** shooting out of your index finger to the selection point.

  * **New Gestures:** During certain UI operations and menus, a screen or menu may appear closer to your face that the regular UI screens. When one of these appears, you are in a special UI mode in which you just **touch** the menu to scroll and **push** your finger through the screen to select items. If you are having problems accessing the menu/items in this mode, try holding your finger vertically instead of parallel to the floor.

  * **Other Notes:**

    * When looking at the device from the top with the displays at the front, there are 4 visible buttons. The two on the left side are brightness and the two on the right side are volume. The **raised** button on each side is the **increase** button and the **indented** button is the **decrease** button.

    * The power button is located on the right side end of the device, opposite from the USB-C port.

    * On the left side of the USB-C port are 5 tiny white status LEDs. Throughout this document, **0** means a lit LED and **x** means an unlit LED.

    * The device will not power down while plugged into USB, or it may power immediately back up by itself. To prepare for any flashing or shut down sequences, you must unplug the USB cable from any power or data source.

    * The device will immediately power on as soon as you connect the USB port to any power or data source.

    * In general, you can use voice commands for every operation in the UI. Usually focusing on an icon or menu option will display the voice command after a couple of seconds, this is usually the same name as the option.

## License and Contributions

Your access to and use of Unreal Engine on GitHub is governed by the [Unreal Engine End User License Agreement](https://www.unrealengine.com/eula). If you don't agree to those terms, as amended from time to time, you are not permitted to access or use Unreal Engine.

We welcome any contributions to Unreal Engine development through [pull requests](https://github.com/EpicGames/UnrealEngine/pulls/) on GitHub. Most of our active development is in the **master** branch, so we prefer to take pull requests there (particularly for new features). We try to make sure that all new code adheres to the [Epic coding standards](https://docs.unrealengine.com/latest/INT/Programming/Development/CodingStandard/).  All contributions are governed by the terms of the EULA.
