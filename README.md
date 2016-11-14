#Dividi: A ComPort TCP/SSL sharing tool

## INTRODUCTION
Dividi is a server application that receives, processes and forwards TCP commands from clients to serial ports/devices in a secure way (SSL). As a consequence:
 
* Multiple applications can communicate with a serial port at the same time.
* Serial ports can be accesed from remote locations in a secure way

![Alt text](doc/dividi.png?raw=true "Architecture")

## CONTINUOUS INTEGRATION
[![Build Status](https://travis-ci.org/roel0/dividi.svg?branch=master)](https://travis-ci.org/roel0/dividi)
    
## USAGE
1. Create a configuration file, for example:
       
       ```
        #COMPORT    #TCPORT        
        /dev/pts/5  1100        
        /dev/pts/6  1200
       ```    
2. Execute dividi

    ```
    dividi -s path/to/config/file
    ```    
3. Setup your clients
