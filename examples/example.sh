sudo build/dividi/dividi --conf ./examples/cert/dividi.cnf --servercert ./examples/cert/server.crt --key ./examples/cert/server.key --rootca ./examples/cert/rootCA.pem &
sleep 1
openssl s_client -connect 127.0.0.1:1100 -cert ./examples/cert/device.crt -key ./examples/cert/device.key
