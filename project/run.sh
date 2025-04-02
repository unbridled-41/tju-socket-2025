docker start Liso
docker exec -it Liso /bin/bash
cd workspace
make
./echo_server
./echo_client 127.0.0.1 9999

docker exec -it Liso /bin/bash
cd workspace
./example ./samples/sample_request_example