# DXRuby64

**DXRuby64** is a custom-built version of DXRuby for 64-bit Ruby 3.1 and later, created by patching the original DXRuby source to allow building with the Universal C Runtime (UCRT). With DXRuby64, you can develop 2D games using DirectX 9 even on 64-bit Ruby environments.

**Note:**
This gem is an unofficial version created by an individual and is not maintained by the original DXRuby project.

[日本語の README はこちら](README-ja.md)

## Installation

To install the gem, simply run:

```bash
gem install dxruby64
```

**Note:**
Please run the above command only when the original DXRuby is not installed, as it may interfere with the operation of DXRuby64.
(The original DXRuby does not support Ruby versions 3.1 and later, so the gem is unlikely to be installed in most environments.)

## Usage
Once installed, you can use DXRuby features as usual:

```ruby
require 'dxruby'

# Example code: Create a window and display a simple shape
Window.width = 640
Window.height = 480

# Draw a rectangle
Window.loop do
  Window.draw(100, 100, Image.new(50, 50, C_WHITE))
end
```

#### About Sample Files
This gem includes simple sample code under the `sample/` directory.
(The files are direct copies of the original examples provided with DXRuby)

The gem installation path may vary depending on your Ruby environment, but you can check the location with the following command:

```
gem contents dxruby64
```

## Requirements
- **Ruby 3.1 or later (64-bit)** installed on Windows.

- Ensure that **`d3dx9_40.dll`** is present. This DLL is part of the DirectX 9 runtime and can be downloaded from [the official Microsoft website](https://www.microsoft.com/en-us/download/details.aspx?id=8109).

#### How to obtain and place `d3dx9_40.dll`

1. Download the file (`directx_Jun2010_redist.exe`) from [DirectX End-User Runtimes (June 2010)](https://www.microsoft.com/en-us/download/details.aspx?id=8109).

2. Double-click the downloaded `directx_Jun2010_redist.exe` and extract the files to a directory of your choice.

3. In the extracted folder, locate `Nov2008_d3dx9_40_x64.cab` and double-click it.

4. Inside the `Nov2008_d3dx9_40_x64.cab` file, locate `d3dx9_40.dll` and copy (or move) it to the `bin` directory of your Ruby installation folder. (e.g. C:\Ruby34-x64\bin)





## About the Sound Class Revision
### Background
The [original DXRuby](https://github.com/mirichi/dxruby) Sound class used a library called `DirectMusic`.
However, on 64-bit environments of Windows 10/11, `DirectMusic` does not function correctly, and the following error has been confirmed:

```
`DXRuby::Sound#initialize': DirectMusic initialize error - CoCreateInstance (DXRuby::DXRubyError)  
```

Therefore, in DXRuby64, we replaced `DirectMusic` with a library called `DirectSound`.
(`DirectSound` is already used in the SoundEffect class and is a proven library.)

### Sample of the Revised Sound Class
A sample using the revised Sound class has been added to the `wav_sample/` directory within the sample files mentioned earlier.
Please give it a try.
Also, be sure to review the following limitations and changes.

### Limitations and Specification Changes
Due to functional constraints of `DirectSound`, the following changes were made as part of the revision:

- The Sound class supports `.wav` , `.mp3` and `.ogg`(Vorbis codec only) file formats
(The [original DXRuby](https://github.com/mirichi/dxruby) supports `.wav` and `.mid`)

- Loop count cannot be specified; only "play once" or "infinite loop" are supported

- Designed with use cases like sound effects and BGM for games in mind, only a minimal set of methods (listed below) is implemented

```
Available Methods (including newly arranged ones)

 .new             Create an object
 #play            Play
 #stop            Stop
 #volume=         Set volume (0 to 255)
 #set_volume      Set volume (alias of #volume=, but the old fade effect via a second argument is not supported)
 #loop=           Set loop mode (same as #loop_count; accepts true or false)
 #loop_count=     Set loop count (only two options supported: 0 = play once, -1 = loop infinitely)
 #dispose         Dispose the object
 #disposed?       Check if the object has been disposed
```
```
Unsupported Methods (implemented in the original DXRuby)

 .load_from_memory
 #start=
 #loop_start=
 #loop_end=
 #pan
 #pan=
 #frequency
 #frequency=
```

## License
This gem is licensed under the zlib/libpng license.

## Disclaimer
This gem is not an official successor of DXRuby, and it is not maintained by the original DXRuby project. It is a custom build intended to work in UCRT environments for newer versions of Ruby.

## Repository
For source code and further information, visit the repository:
https://github.com/spoolkitamura/dxruby64

## Credits

This software uses [stb_vorbis](https://github.com/nothings/stb) by Sean T. Barrett.  
stb_vorbis is dual-licensed under public domain and the MIT license.

<br>
<br>
<br>

## Developer Guide
### Building dxruby.so from Source
Although the gem includes precompiled .so files, developers who wish to build dxruby.so from source may refer to the steps below as a guide.

#### Prerequisites
- Ruby 3.1 or later (64-bit) must be installed.

- A development environment such as MSYS2 is required.
(RubyInstaller with Devkit is recommended)

#### Build Steps
Clone the repository:

```
git clone https://github.com/spoolkitamura/dxruby64.git
cd dxruby64
```

Move into the ext/dxruby64 folder and run the following commands to build dxruby.so:

```
cd ext/dxruby64
ridk enable
ruby extconf.rb       # Generate Makefile
make clean
make                  # Build dxruby.so
```

The built dxruby.so file will be installed into Ruby's extension library directory (e.g., C:/Ruby31-x64/lib/ruby/site_ruby/3.1.0/x64-ucrt/) by executing `make install`.
You can check the exact path using `RbConfig::CONFIG["sitearchdir"]`.

### Notes
- The original DXRuby source code is located in ext/org.

- In this gem, prebuilt dxruby.so files for each supported Ruby version are organized in the following directory structure:

```
lib/
  dxruby.rb     - Dynamically loads the appropriate version-specific dxruby.so
  31/
    dxruby.so   - For Ruby 3.1
  32/
    dxruby.so   - For Ruby 3.2
  33/
    dxruby.so   - For Ruby 3.3
  34/
    dxruby.so   - For Ruby 3.4
```

