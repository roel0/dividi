sudo build/dividi/dividi -s ./examples/cert/dividi.cnf -c ./examples/cert/server.crt -k ./examples/cert/server.key -r ./examples/cert/rootCA.pem &
sleep 1
openssl s_client -connect 127.0.0.1:1100 -cert ./examples/cert/device.crt -key ./examples/cert/device.key
