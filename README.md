#Dividi: A ComPort TCP/SSL sharing tool

## INTRODUCTION
Dividi is a server application that sets up a full-duplex communication pipe (SSL secured) between a serial port and a tcp port. Clients can communicate with the serial device over this pipe.
 
* Multiple applications can communicate with a serial port at the same time.
* Serial ports can be accesed from remote locations in a secure way (SSL)

![Alt text](doc/dividi.png?raw=true "Architecture")

## CONTINUOUS INTEGRATION

[![Build Status](https://travis-ci.org/roel0/dividi.svg?branch=master)](https://travis-ci.org/roel0/dividi)

[![Coverity](https://scan.coverity.com/projects/10812/badge.svg)](https://scan.coverity.com/projects/roel0-dividi)
    
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
