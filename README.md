#Dividi: A ComPort TCP sharing tool
- - - -

## INTRODUCTION
Dividi is a server application that receives, processes and forwards TCP commands from clients to serial ports/devices. As a consequence:

* Multiple applications can communicate with a serial port at the same time.
* Serial ports can be accesed from remote locations

![Alt text](doc/dividi.png?raw=true "Architecture")

## BUILDING
    make
    
## USAGE

  1. Create a configuration file, for example:
```
    #COMPORT    #TCPORT
    /dev/pts/5  1100
    /dev/pts/6  1200
```    
  2. Execute dividi
```
    dividi -c path/to/config/file
```
  3. Setup your clients
