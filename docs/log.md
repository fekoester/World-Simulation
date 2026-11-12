# Development log (2018)

The original notes from `TODO List and log.h`.

- Make all constants changeable by the organism (e.g. the energy needed for offspring).
- All equations have all variables in them with constants and exponentials which can be written in a second config file.

**19.08.2018**
- Organisms still don't have a real evolution. Have to find the right parameters.

**25.11.2018**
- The border of the map now does not have any food. This prevents crowding at the borders. The more beautiful solution would be to find a set of equations/parameters that gives an advantage for staying inland. (Maybe try an island setup?)
- There seems to be too much random behaviour. I guess the parameter space is just too big. Maybe I should try a slim version where there is only food with an easy set of parameters. (Maybe try that while moving an organism can't eat?)

**8.12.2018**
- Changing the movement behaviour and visuals of the organisms. Instead of a fixed coordinate system, let's try a relative one, where the organisms can rotate. Makes more sense.
- Implement simple food and temperature rules. ODEs are computationally costly.
