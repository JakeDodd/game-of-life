# A Terminal version of the Game of Life
## Installing and Running
There is currently no installation for this application

Pull the repo down

Run `make` to compile the source

Run the app with `./play-life`

Add an optional filename arguement to run a save state

## Playing the Game
Use **h,j,k,l** to move the cursor around

Use **t** to toggle the node at the cursor between alive and dead

Press **[ENTER]** to start the life cycle

Game states can be saved with **Ctrl-S** 

Use **Ctrl-Q** to quit

## Known Issues
The app currently runs 1000 generations with a 200 ms delay between generations. There is no way to quit while these generations are running.

Certain configurations of cells cause a seg fault.
