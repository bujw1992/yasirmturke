/*
 * This file is part of the libserialport project.
 *
 * Copyright (C) 2013 Martin Ling <martin-libserialport@earth.li>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBSERIALPORT_H
#define LIBSERIALPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* A serial port. */
struct sp_port {
	/* Name used to open the port */
	char *name;
	/* OS-specific port handle */
#ifdef _WIN32
	HANDLE hdl;
#else
	int fd;
#endif
};

/* Return values. */
enum {
	/* Operation completed successfully. */
	SP_OK = 0,
	/* Invalid arguments were passed to the function. */
	SP_ERR_ARG = -1,
	/* A system error occured while executing the operation. */
	SP_ERR_FAIL = -2,
	/* A memory allocation failed while executing the operation. */
	SP_ERR_MEM = -3,
};

/* Port access modes. */
enum {
	/* Open port for read/write access. */
	SP_MODE_RDWR = 1,
	/* Open port for read access only. */
	SP_MODE_RDONLY = 2,
	/* Open port in non-blocking mode. */
	SP_MODE_NONBLOCK = 4,
};

/* Parity settings. */
enum {
	/* No parity. */
	SP_PARITY_NONE = 0,
	/* Even parity. */
	SP_PARITY_EVEN = 1,
	/* Odd parity. */
	SP_PARITY_ODD = 2,
};

/* Flow control settings. */
enum {
	/* No flow control. */
	SP_FLOW_NONE = 0,
	/* Hardware (RTS/CTS) flow control. */
	SP_FLOW_HARDWARE = 1,
	/* Software (XON/XOFF) flow control. */
	SP_FLOW_SOFTWARE = 2,
};

int sp_get_port_by_name(const char *portname, struct sp_port **port_ptr);
void sp_free_port(struct sp_port *port);
int sp_list_ports(struct sp_port ***list_ptr);
int sp_copy_port(const struct sp_port *port, struct sp_port **copy_ptr);
void sp_free_port_list(struct sp_port **ports);
int sp_open(struct sp_port *port, int flags);
int sp_close(struct sp_port *port);
int sp_flush(struct sp_port *port);
int sp_write(struct sp_port *port, const void *buf, size_t count);
int sp_read(struct sp_port *port, void *buf, size_t count);
int sp_set_params(struct sp_port *port, int baudrate, int bits, int parity,
		int stopbits, int flowcontrol, int rts, int dtr);
int sp_last_error_code(void);
char *sp_last_error_message(void);
void sp_free_error_message(char *message);

#ifdef __cplusplus
}
#endif

#endif /* LIBSERIALPORT_H */
