# Kanan for MabiPro [![Build status](https://ci.appveyor.com/api/projects/status/frp20d9y14rpxha0?svg=true)](https://ci.appveyor.com/project/cursey/kanan-new)
A fork of the Kanan for Mabinogi for use with MabiPro

## Download
Downloads for the latest _official_ releases are located [here](https://github.com/ryuugana/kanan-mabipro/releases).

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

## Usage
Put dsound.dll and patches.json into your MabiPro folder. Press the Insert button in game to enable and disable mods.

## This project is still a work in progress
I may periodically release official binaries for those who don't want to build the project themselves.

## Todo
* Add more patches and mods
* Integrate features from original Kanan that do not currently work on MabiPro

## Preview from the very first public version
![Preview](preview.png)

## Many original patches/ideas came from the following projects:
* Fantasia
* MAMP
* JAP
* Gerent/GerentxNogi
* MNG
* Noginogi-Party
* DataCami

## Link to original Kanan
* [Kanan's New Mabinogi Mod](https://github.com/cursey/kanan-new) - Mods for the official version of Mabinogi
