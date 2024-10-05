import socket

def make_request(host, request, port=8080):
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client_socket.connect((host, port))
    client_socket.sendall(request)
    response = b""
    while True:
        data = client_socket.recv(4096)
        if not data:
            break
        response += data
    client_socket.close()
    return response.decode()

host: str = "localhost"
path: str = "/"
request: str = f"GET {path} HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n"
response = make_request(host, request.encode("utf-8"), 8080)
print(response)
request: str = f"GET {path} HTTP/1.1\nHost: {host}\nConnection: close\n\n"
response = make_request(host, request.encode("utf-8"), 8080)
print(response)
request: str = f"asd9777as09d7 097as0d\r\r\r\r"
response = make_request(host, request.encode("utf-8"), 8080)
print(response)
