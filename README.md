# Kanan for MabiPro
A fork of [Kanan's New Mabinogi Mod](https://github.com/cursey/kanan-new) that provides a variety of mods for G13 Mabinogi and fully supports [AstralWorld Patcher](https://github.com/slargi/FantasiaG13).

## Download
[The latest KananMabiPro.zip can be downloaded here](https://github.com/ryuugana/kanan-mabipro/releases/latest/download/KananMabiPro.zip)!

## Install
* Put dsound.dll and patches.json into your MabiPro folder 
* Press the INSERT key on your keyboard while in game to enable and disable mods
* Pressing INSERT to close Kanan will also save your mod selection

## Requirements
You need the Microsoft Visual C++ Redistributable for Visual Studio 2017 available at the bottom of [this page](https://www.visualstudio.com/downloads/). Make sure to choose the x86 version of the download!

## What's new
* Entirely written from the ground up in modern C++17. 
* Overall design was kept simple and *most* of it is commented so beginners can understand whats going on.
* Core memory hacking library is seperate from the Mabinogi specific portion and can be easily reused for other games.
* Includes an ingame UI to configure each mod.
* Includes a reusable Direct3D 9 hook.
* Includes a reusable DirectInput 8 hook.
* Intercepts windows messages sent to the game window.
* Reverse engineered game structures to allow for unique features such as entity viewing.
* A simple and secure Launcher for easy multi-client (WIP)
	* This project makes use of WinHTTP, Cryptography Next Generation (CNG), and WMI APIs
* Probably other things!

## Build requirements
* Visual Studio 2017

## This project is still a work in progress
I may periodically release official binaries for those who don't want to build the project themselves.

## Todo
* Add more patches and mods
* Integrate features from original Kanan that do not currently work on MabiPro
* Vulkan support for use with [dxvk](https://github.com/doitsujin/dxvk)

## Preview
![preview](https://github.com/user-attachments/assets/acf53269-e3e7-4ede-8d88-f125a1c99276)


![preview](https://github.com/user-attachments/assets/7e684f6b-0462-4428-acc1-39d2f489cf50)


![preview](https://github.com/user-attachments/assets/fc7b829d-f707-4dfc-9bef-f6189ee1865d)



## Many original patches/ideas came from the following projects:
* Fantasia
* MAMP
* JAP
* Gerent/GerentxNogi
* MNG
* Noginogi-Party
* DataCami
