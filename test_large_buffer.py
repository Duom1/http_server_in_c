import requests

y: list[str] = []
for _ in range(1024 * 8):
    y.append("kidnr7vya5g48vodi58v")

x: str = "".join(y)

response = requests.get("http://localhost:8080", params={"jarkko": x})

print(response.content)
