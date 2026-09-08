# TcpServer
Purpose
-------
Provide the TCP listening entry point of the server.

Responsibilities
----------------
- Create the listening socket.
- Bind the socket to the configured IP address and port.
- Start listening for incoming TCP connections.
- Accept new client connections.
- Transfer accepted client sockets to the connection-handling layer.
- Manage the lifecycle of the listening socket.

Owns
----
- server_fd
- 
Does NOT handle
---------------
- Reading application data from client connections.
- Writing application responses.
- HTTP parsing.
- HTTP routing.
- Business logic.
- Authentication.
- Database access.

Dependencies
------------
- Linux Socket API

Output
------
Accepted client socket (client_fd)


# Connection
Purpose
-------
Represent and manage one established TCP connection.

Responsibilities
----------------
- Own the accepted client socket.
- Receive raw bytes from the client.
- Send raw bytes to the client.
- Manage the connection lifecycle.
- Close the socket when the connection ends.

Owns
----
- client_fd
- 
Does NOT handle
---------------
- Accepting new connections.
- HTTP parsing.
- Routing.
- Business logic.
- Authentication.
- Database access.

Input
-----
Accepted client socket from TcpServer.

Output
------
Raw byte stream to the protocol layer.

# HTTP Module

Purpose
-------
Translate between HTTP wire format and
application-level HTTP objects.

Responsibilities
----------------
- Parse raw HTTP request bytes.
- Construct HttpRequest objects.
- Represent HTTP request data.
- Represent HTTP response data.
- Serialize HttpResponse into HTTP wire format.

Owns / Defines
--------------
- HttpRequest
- HttpResponse
- HttpParser

Does NOT handle
---------------
- TCP connection management.
- Routing.
- Business logic.
- Authentication.
- Database access.
- Business meaning of request bodies.

Input
-----
Raw bytes from Connection.

Output
------
HttpRequest to the application layer.

Reverse Output
--------------
Serialized HTTP response bytes to Connection.

# Handler
## Purpose

Adapt a specific HTTP request into an application operation and convert the operation result into an HTTP response.

### Responsibilities
Extract required input from HttpRequest.
Perform request-level input validation.
Invoke the appropriate Service operation.
Convert application results and errors into HttpResponse.
### Non-Responsibilities
Route matching.
HTTP protocol parsing.
Core business rules.
Direct database access.
TCP connection management.
### Input
HttpRequest.
### Output
HttpResponse.
### Dependencies
HTTP module.
Service layer.
### Resource Ownership

Handlers normally do not own network or persistence resources.

### Boundary

Handler defines how a specific HTTP request is translated into an application operation and how the result of that operation is translated back into an HTTP response. Core business rules remain the responsibility of the Service layer.