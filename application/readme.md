Flow application components
============================

This page describes the subsystems involved in the "Flow" application (later refered to as Flow), and the contents of this folder.

### Subsystems

The "Flow" application features the following subsystems running on different devices:
  * **Person tracking**
    * Hardware: Ubuntu box running OpenPTrack with NDN producer 
    * Functionality: produces the coordinates (x, y, z) of each person tracked in the physical space
    * For more details, see [openptrack](openptrack) folder
  * **Virtual camera control**
    * Hardware: gyroscopes attached to RFduinos, and a RaspberryPi to help serve NDN data to the rest of the system 
    * Functionality: produces the facing (pitch, yaw, roll) of the gyroscopes
    * For more details, see [rfduino](rfduino) folder
  * **Web interface image dropping**
    * Hardware: mobile phones running Firefox browser
    * Functionality: sends command to the visualization in Unity
    * For more details, see [website](website) folder
  * **Visualization**
    * Hardware: OS X machine running Unity
    * Functionality: Renders the virtual space based on data from person tracking, virtual camera control, and command from the web interface.
    * For more details, see [unity](unity) folder

### Supplementary materials

* A diagram of the application installation is available [here](https://github.com/remap/ndn-flow/blob/master/design/flow-components.pdf).
* This folder also contains [sample code](sample-code) that may help coding or debugging each subsystem individually.
* The framework we built this application on can be found [here](https://github.com/remap/ndn-flow/tree/master/framework), with its interfaces described [here](https://github.com/remap/ndn-flow/tree/master/design/docs).

### License
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program in the file COPYING.  If not, see <http://www.gnu.org/licenses/>.
