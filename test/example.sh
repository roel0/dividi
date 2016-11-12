sudo build/dividi/dividi -s ./build/test/dummy/dividi.cnf -c ./build/test/dummy/device.crt -k ./build/test/dummy/device.key -r ./build/test/dummy/rootCA.pem &
sleep 1
openssl s_client -connect 127.0.0.1:1100 -cert ./build/test/dummy/device.crt -key ./build/test/dummy/device.key
