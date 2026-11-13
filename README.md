Lighter, Linux native CADe SIMU editor. 
I got tired of CADe SIMU being shit, so this vibe coded slop is slightly less shit and runs natively on penguin os. you can probally also run it on windows but u gotta build it yourself, might make a windows build later if i feel like it. 

Features:
Fast as fuck boiiii. Simulations that take more than a second to tick through on CADe SIMU have imperceptable delay on my program. 
Open .cad documents. 
Write .cad documents. 
probally some other stuff idk might update this later

Requirements:
- CMake 3.21+
- Ninja (or another generator)
- Qt 6.5+ with the Widgets module
- A C++20 compiler

Build:
`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`
`cmake --build build`

Run:
You know how to run a program. 

