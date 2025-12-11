#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/ip.h>

typedef struct s_state {
	int		listen_fd;				// Server's listening socket file descriptor
	int		highest_fd;				// Highest file descriptor for select() optimization
	fd_set	active_fds;				// Set of active file descriptors being monitored
	int		next_client_id;			// Auto-incrementing ID for the next connecting client
	int		fd_to_id[65536];		// Maps file descriptor to client ID
	char	*fd_to_buffer[65536];	// Maps file descriptor to client's message buffer
} t_state;

t_state g_state;			// Global server state
char g_recv_buffer[1001];	// Buffer for receiving data from clients
char g_message_buffer[128];	// Buffer for formatting broadcast messages

// from the subject
int extract_message(char **buf, char **msg)
{
	char *newbuf;
	int i = 0;

	*msg = NULL;
	if (!*buf)
		return (0);
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, strlen(*buf + i + 1) + 1);
			if (!newbuf)
				return (-1);
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = '\0';
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

// from the subject
char *str_join(char *buf, char *add)
{
	char *newbuf;
	int len;

	len = 0;
	if (buf)
		len = strlen(buf);
	newbuf = malloc(len + strlen(add) + 1);
	if (!newbuf)
		return (NULL);
	newbuf[0] = '\0';
	if (buf)
		strcat(newbuf, buf);
	strcat(newbuf, add);
	free(buf);
	return (newbuf);
}

// fatal - Emergency exit on unrecoverable error
void fatal(void) {
	write(2, "Fatal error\n", 12);
	exit(1);
}

// broadcast - Send message to all connected clients except sender
void broadcast(int sender_fd, char *message) {
	for (int fd = 0; fd <= g_state.highest_fd; fd++) {
		if (FD_ISSET(fd, &g_state.active_fds) && fd != sender_fd && fd != g_state.listen_fd) {
			send(fd, message, strlen(message), 0);
		}
	}
}

// client_join - Register new client connection
void client_join(int client_fd) {
	// Stage 1: Update server state for new client
	if (client_fd > g_state.highest_fd) {
		g_state.highest_fd = client_fd;
	}

	// Stage 2: Assign unique client ID and initialize buffer
	g_state.fd_to_id[client_fd] = g_state.next_client_id++;
	g_state.fd_to_buffer[client_fd] = NULL;
	FD_SET(client_fd, &g_state.active_fds);

	// Stage 3: Broadcast arrival notification to all other clients
	sprintf(g_message_buffer, "server: client %d just arrived\n", g_state.fd_to_id[client_fd]);
	broadcast(client_fd, g_message_buffer);
}


// client_leave - Handle client disconnection
void client_leave(int client_fd) {
	// Stage 1: Broadcast departure notification
	sprintf(g_message_buffer, "server: client %d just left\n", g_state.fd_to_id[client_fd]);
	broadcast(client_fd, g_message_buffer);

	// Stage 2: Clean up client resources
	free(g_state.fd_to_buffer[client_fd]);
	FD_CLR(client_fd, &g_state.active_fds);
	close(client_fd);
}

// client_message - Process and broadcast complete messages from client
void client_message(int client_fd) {
	char *line;
	int extract_result;

	// Stage 1: Extract and broadcast all complete messages
	while ((extract_result = extract_message(&g_state.fd_to_buffer[client_fd], &line)) == 1) {
		sprintf(g_message_buffer, "client %d: ", g_state.fd_to_id[client_fd]);
		broadcast(client_fd, g_message_buffer);
		broadcast(client_fd, line);
		free(line);
	}

	// Stage 2: Handle extraction errors
	if (extract_result < 0) {
		fatal();
	}
}


// setup_server - Create and configure listening socket
int server_setup(int port) {
	// Stage 1: Create TCP socket
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		fatal();
	}

	// Stage 2: Configure server address (127.0.0.1:port)
	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(2130706433);
	addr.sin_port = htons(port);

	// Stage 3: Bind socket to address
	if (bind(sockfd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fatal();
	}

	// Stage 4: Start listening for connections
	if (listen(sockfd, 128) < 0) {
		fatal();
	}

	return (sockfd);
}

// server_loop - Main server event loop using select()
void server_loop(void) {
	fd_set				ready_fds;
	struct sockaddr_in	client_addr;
	socklen_t			addr_len;

	while (1) {
		// Stage 1: Prepare file descriptor sets for monitoring
		ready_fds = g_state.active_fds;

		// Stage 2: Wait for activity on any file descriptor
		if (select(g_state.highest_fd + 1, &ready_fds, NULL, NULL, NULL) < 0) {
			fatal();
		}

		// Stage 3: Process all file descriptors with activity
		for (int fd = 0; fd <= g_state.highest_fd; fd++) {
			if (!FD_ISSET(fd, &ready_fds)) {
				continue;
			}

			// Stage 4a: Handle new client connection
			if (fd == g_state.listen_fd) {
				addr_len = sizeof(client_addr);
				int client_fd = accept(g_state.listen_fd, (struct sockaddr *)&client_addr, &addr_len);
				if (client_fd >= 0) {
					client_join(client_fd);
				}
			}
			// Stage 4b: Handle existing client data
			else {
				int bytes = recv(fd, g_recv_buffer, 1000, 0);

				// Stage 5a: Client disconnected or error
				if (bytes <= 0) {
					client_leave(fd);
					continue;
				}

				// Stage 5b: Process received data
				g_recv_buffer[bytes] = '\0';

				g_state.fd_to_buffer[fd] = str_join(g_state.fd_to_buffer[fd], g_recv_buffer);
				if (!g_state.fd_to_buffer[fd]) {
					fatal();
				}

				// Stage 5c: Extract and broadcast complete messages
				client_message(fd);
			}
		}
	}
}

int main(int argc, char **argv) {
	// Stage 1: Validate command line arguments
	if (argc != 2)
	{
		write(2, "Wrong number of arguments\n", 26);
		return (1);
	}

	// Stage 2: Initialize server state
	bzero(&g_state, sizeof(g_state));
	FD_ZERO(&g_state.active_fds);

	// Stage 3: Create and configure listening socket
	g_state.listen_fd = server_setup(atoi(argv[1]));

	// Stage 4: Add listening socket to monitoring set
	g_state.highest_fd = g_state.listen_fd;
	FD_SET(g_state.listen_fd, &g_state.active_fds);

	// Stage 5: Enter main event loop (never returns)
	server_loop();

	return (0);
}

