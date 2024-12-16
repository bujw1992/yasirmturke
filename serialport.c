/*
 * This file is part of the libserialport project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2010-2012 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#include <tchar.h>
#else
#include <termios.h>
#include <sys/ioctl.h>
#endif
#ifdef __APPLE__
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <sys/syslimits.h>
#endif
#ifdef __linux__
#include "libudev.h"
#include "linux/serial.h"
#endif

#include "libserialport.h"

int sp_get_port_by_name(const char *portname, struct sp_port **port_ptr)
{
	struct sp_port *port;
	int len;

	if (!port_ptr)
		return SP_ERR_ARG;

	*port_ptr = NULL;

	if (!portname)
		return SP_ERR_ARG;

	if (!(port = malloc(sizeof(struct sp_port))))
		return SP_ERR_MEM;

	len = strlen(portname) + 1;

	if (!(port->name = malloc(len)))
	{
		free(port);
		return SP_ERR_MEM;
	}

	memcpy(port->name, portname, len);

	*port_ptr = port;

	return SP_OK;
}

int sp_copy_port(const struct sp_port *port, struct sp_port **copy_ptr)
{
	if (!copy_ptr)
		return SP_ERR_ARG;

	*copy_ptr = NULL;

	if (!port || !port->name)
		return SP_ERR_ARG;

	return sp_get_port_by_name(port->name, copy_ptr);
}

void sp_free_port(struct sp_port *port)
{
	if (!port)
		return;

	if (port->name)
		free(port->name);

	free(port);
}

static struct sp_port **sp_list_append(struct sp_port **list, const char *portname)
{
	void *tmp;
	unsigned int count;
	for (count = 0; list[count]; count++);
	if (!(tmp = realloc(list, sizeof(struct sp_port *) * (count + 2))))
		goto fail;
	list = tmp;
	if (sp_get_port_by_name(portname, &list[count]) != SP_OK)
		goto fail;
	list[count + 1] = NULL;
	return list;
fail:
	sp_free_port_list(list);
	return NULL;
}

int sp_list_ports(struct sp_port ***list_ptr)
{
	struct sp_port **list;
	int ret = SP_OK;

	if (!(list = malloc(sizeof(struct sp_port **))))
		return SP_ERR_MEM;;

	list[0] = NULL;

#ifdef _WIN32
	HKEY key;
	TCHAR *value, *data;
	DWORD max_value_len, max_data_size, max_data_len;
	DWORD value_len, data_size, data_len;
	DWORD type, index = 0;
	char *name;
	int name_len;

	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("HARDWARE\\DEVICEMAP\\SERIALCOMM"),
			0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
	{
		ret = SP_ERR_FAIL;
		goto out_done;
	}
	if (RegQueryInfoKey(key, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
				&max_value_len, &max_data_size, NULL, NULL) != ERROR_SUCCESS)
	{
		ret = SP_ERR_FAIL;
		goto out_close;
	}
	max_data_len = max_data_size / sizeof(TCHAR);
	if (!(value = malloc((max_value_len + 1) * sizeof(TCHAR))))
	{
		ret = SP_ERR_MEM;
		goto out_close;
	}
	if (!(data = malloc((max_data_len + 1) * sizeof(TCHAR))))
	{
		ret = SP_ERR_MEM;
		goto out_free_value;
	}
	while (
		value_len = max_value_len + 1,
		data_size = max_data_size,
		RegEnumValue(key, index, value, &value_len,
			NULL, &type, (LPBYTE)data, &data_size) == ERROR_SUCCESS)
	{
		data_len = data_size / sizeof(TCHAR);
		data[data_len] = '\0';
#ifdef UNICODE
		name_len = WideCharToMultiByte(CP_ACP, 0, data, -1, NULL, 0, NULL, NULL)
#else
		name_len = data_len + 1;
#endif
		if (!(name = malloc(name_len)))
		{
			ret = SP_ERR_MEM;
			goto out;
		}
#ifdef UNICODE
		WideCharToMultiByte(CP_ACP, 0, data, -1, name, name_len, NULL, NULL);
#else
		strcpy(name, data);
#endif
		if (type == REG_SZ && !(list = sp_list_append(list, name)))
		{
			ret = SP_ERR_MEM;
			goto out;
		}
		index++;
	}
out:
	free(data);
out_free_value:
	free(value);
out_close:
	RegCloseKey(key);
out_done:
#endif
#ifdef __APPLE__
	mach_port_t master;
	CFMutableDictionaryRef classes;
	io_iterator_t iter;
	char *path;
	io_object_t port;
	CFTypeRef cf_path;
	Boolean result;

	if (IOMasterPort(MACH_PORT_NULL, &master) != KERN_SUCCESS)
	{
		ret = SP_ERR_FAIL;
		goto out_done;
	}

	if (!(classes = IOServiceMatching(kIOSerialBSDServiceValue)))
	{
		ret = SP_ERR_FAIL;
		goto out_done;
	}

	CFDictionarySetValue(classes,
			CFSTR(kIOSerialBSDTypeKey), CFSTR(kIOSerialBSDAllTypes));

	if (IOServiceGetMatchingServices(master, classes, &iter) != KERN_SUCCESS)
	{
		ret = SP_ERR_FAIL;
		goto out_done;
	}

	if (!(path = malloc(PATH_MAX)))
	{
		ret = SP_ERR_MEM;
		goto out_release;
	}

	while ((port = IOIteratorNext(iter))) {
		cf_path = IORegistryEntryCreateCFProperty(port,
				CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0);
		if (cf_path) {
			result = CFStringGetCString(cf_path,
					path, PATH_MAX, kCFStringEncodingASCII);
			CFRelease(cf_path);
			if (result && !(list = sp_list_append(list, path)))
			{
				ret = SP_ERR_MEM;
				IOObjectRelease(port);
				goto out;
			}
		}
		IOObjectRelease(port);
	}
out:
	free(path);
out_release:
	IOObjectRelease(iter);
out_done:
#endif
#ifdef __linux__
	struct udev *ud;
	struct udev_enumerate *ud_enumerate;
	struct udev_list_entry *ud_list;
	struct udev_list_entry *ud_entry;
	const char *path;
	struct udev_device *ud_dev, *ud_parent;
	const char *name;
	const char *driver;
	int fd, ioctl_result;
	struct serial_struct serial_info;

	ud = udev_new();
	ud_enumerate = udev_enumerate_new(ud);
	udev_enumerate_add_match_subsystem(ud_enumerate, "tty");
	udev_enumerate_scan_devices(ud_enumerate);
	ud_list = udev_enumerate_get_list_entry(ud_enumerate);
	udev_list_entry_foreach(ud_entry, ud_list)
	{
		path = udev_list_entry_get_name(ud_entry);
		ud_dev = udev_device_new_from_syspath(ud, path);
		/* If there is no parent device, this is a virtual tty. */
		ud_parent = udev_device_get_parent(ud_dev);
		if (ud_parent == NULL)
		{
			udev_device_unref(ud_dev);
			continue;
		}
		name = udev_device_get_devnode(ud_dev);
		/* The serial8250 driver has a hardcoded number of ports.
		 * The only way to tell which actually exist on a given system
		 * is to try to open them and make an ioctl call. */
		driver = udev_device_get_driver(ud_parent);
		if (driver && !strcmp(driver, "serial8250"))
		{
			if ((fd = open(name, O_RDWR | O_NONBLOCK | O_NOCTTY)) < 0)
				goto skip;
			ioctl_result = ioctl(fd, TIOCGSERIAL, &serial_info);
			close(fd);
			if (ioctl_result != 0)
				goto skip;
			if (serial_info.type == PORT_UNKNOWN)
				goto skip;
		}
		list = sp_list_append(list, name);
skip:
		udev_device_unref(ud_dev);
		if (!list)
		{
			ret = SP_ERR_MEM;
			goto out;
		}
	}
out:
	udev_enumerate_unref(ud_enumerate);
	udev_unref(ud);
#endif

	if (ret == SP_OK)
	{
		*list_ptr = list;
	}
	else
	{
		if (list)
			sp_free_port_list(list);

		*list_ptr = NULL;
	}

	return ret;
}

void sp_free_port_list(struct sp_port **list)
{
	unsigned int i;
	for (i = 0; list[i]; i++)
		sp_free_port(list[i]);
	free(list);
}

static int sp_validate_port(struct sp_port *port)
{
	if (port == NULL)
		return 0;
#ifdef _WIN32
	if (port->hdl == INVALID_HANDLE_VALUE)
		return 0;
#else
	if (port->fd < 0)
		return 0;
#endif
	return 1;
}

#define CHECK_PORT() do { if (!sp_validate_port(port)) return SP_ERR_ARG; } while (0)

int sp_open(struct sp_port *port, int flags)
{
	if (!port)
		return SP_ERR_ARG;

#ifdef _WIN32
	DWORD desired_access = 0, flags_and_attributes = 0;
	char *escaped_port_name;

	/* Prefix port name with '\\.\' to work with ports above COM9. */
	if (!(escaped_port_name = malloc(strlen(port->name + 5))))
		return SP_ERR_MEM;
	sprintf(escaped_port_name, "\\\\.\\%s", port->name);

	/* Map 'flags' to the OS-specific settings. */
	desired_access |= GENERIC_READ;
	flags_and_attributes = FILE_ATTRIBUTE_NORMAL;
	if (flags & SP_MODE_RDWR)
		desired_access |= GENERIC_WRITE;
	if (flags & SP_MODE_NONBLOCK)
		flags_and_attributes |= FILE_FLAG_OVERLAPPED;

	port->hdl = CreateFile(escaped_port_name, desired_access, 0, 0,
			 OPEN_EXISTING, flags_and_attributes, 0);

	free(escaped_port_name);

	if (port->hdl == INVALID_HANDLE_VALUE)
		return SP_ERR_FAIL;
#else
	int flags_local = 0;
	/* Map 'flags' to the OS-specific settings. */
	if (flags & SP_MODE_RDWR)
		flags_local |= O_RDWR;
	if (flags & SP_MODE_RDONLY)
		flags_local |= O_RDONLY;
	if (flags & SP_MODE_NONBLOCK)
		flags_local |= O_NONBLOCK;

	if ((port->fd = open(port->name, flags_local)) < 0)
		return SP_ERR_FAIL;
#endif

	return SP_OK;
}

int sp_close(struct sp_port *port)
{
	CHECK_PORT();

#ifdef _WIN32
	/* Returns non-zero upon success, 0 upon failure. */
	if (CloseHandle(port->hdl) == 0)
		return SP_ERR_FAIL;
#else
	/* Returns 0 upon success, -1 upon failure. */
	if (close(port->fd) == -1)
		return SP_ERR_FAIL;
#endif

	return SP_OK;
}

int sp_flush(struct sp_port *port)
{
	CHECK_PORT();

#ifdef _WIN32
	/* Returns non-zero upon success, 0 upon failure. */
	if (PurgeComm(port->hdl, PURGE_RXCLEAR | PURGE_TXCLEAR) == 0)
		return SP_ERR_FAIL;
#else
	/* Returns 0 upon success, -1 upon failure. */
	if (tcflush(port->fd, TCIOFLUSH) < 0)
		return SP_ERR_FAIL;
#endif
	return SP_OK;
}

int sp_write(struct sp_port *port, const void *buf, size_t count)
{
	CHECK_PORT();

	if (!buf)
		return SP_ERR_ARG;

#ifdef _WIN32
	DWORD written = 0;
	/* Returns non-zero upon success, 0 upon failure. */
	if (WriteFile(port->hdl, buf, count, &written, NULL) == 0)
		return SP_ERR_FAIL;
	return written;
#else
	/* Returns the number of bytes written, or -1 upon failure. */
	ssize_t written = write(port->fd, buf, count);
	if (written < 0)
		return SP_ERR_FAIL;
	else
		return written;;
#endif
}

int sp_read(struct sp_port *port, void *buf, size_t count)
{
	CHECK_PORT();

	if (!buf)
		return SP_ERR_ARG;

#ifdef _WIN32
	DWORD bytes_read = 0;
	/* Returns non-zero upon success, 0 upon failure. */
	if (ReadFile(port->hdl, buf, count, &bytes_read, NULL) == 0)
		return SP_ERR_FAIL;
	return bytes_read;
#else
	ssize_t bytes_read;
	/* Returns the number of bytes read, or -1 upon failure. */
	if ((bytes_read = read(port->fd, buf, count)) < 0)
		return SP_ERR_FAIL;
	return bytes_read;
#endif
}

int sp_set_params(struct sp_port *port, int baudrate,
			      int bits, int parity, int stopbits,
			      int flowcontrol, int rts, int dtr)
{
	CHECK_PORT();

#ifdef _WIN32
	DCB dcb;

	if (!GetCommState(port->hdl, &dcb))
		return SP_ERR_FAIL;

	switch (baudrate) {
	/*
	 * The baudrates 50/75/134/150/200/1800/230400/460800 do not seem to
	 * have documented CBR_* macros.
	 */
	case 110:
		dcb.BaudRate = CBR_110;
		break;
	case 300:
		dcb.BaudRate = CBR_300;
		break;
	case 600:
		dcb.BaudRate = CBR_600;
		break;
	case 1200:
		dcb.BaudRate = CBR_1200;
		break;
	case 2400:
		dcb.BaudRate = CBR_2400;
		break;
	case 4800:
		dcb.BaudRate = CBR_4800;
		break;
	case 9600:
		dcb.BaudRate = CBR_9600;
		break;
	case 14400:
		dcb.BaudRate = CBR_14400; /* Not available on Unix? */
		break;
	case 19200:
		dcb.BaudRate = CBR_19200;
		break;
	case 38400:
		dcb.BaudRate = CBR_38400;
		break;
	case 57600:
		dcb.BaudRate = CBR_57600;
		break;
	case 115200:
		dcb.BaudRate = CBR_115200;
		break;
	case 128000:
		dcb.BaudRate = CBR_128000; /* Not available on Unix? */
		break;
	case 256000:
		dcb.BaudRate = CBR_256000; /* Not available on Unix? */
		break;
	default:
		return SP_ERR_ARG;
	}

	switch (stopbits) {
	/* Note: There's also ONE5STOPBITS == 1.5 (unneeded so far). */
	case 1:
		dcb.StopBits = ONESTOPBIT;
		break;
	case 2:
		dcb.StopBits = TWOSTOPBITS;
		break;
	default:
		return SP_ERR_ARG;
	}

	switch (parity) {
	/* Note: There's also SPACEPARITY, MARKPARITY (unneeded so far). */
	case SP_PARITY_NONE:
		dcb.Parity = NOPARITY;
		break;
	case SP_PARITY_EVEN:
		dcb.Parity = EVENPARITY;
		break;
	case SP_PARITY_ODD:
		dcb.Parity = ODDPARITY;
		break;
	default:
		return SP_ERR_ARG;
	}

	if (rts != -1) {
		if (rts)
			dcb.fRtsControl = RTS_CONTROL_ENABLE;
		else
			dcb.fRtsControl = RTS_CONTROL_DISABLE;
	}

	if (dtr != -1) {
		if (dtr)
			dcb.fDtrControl = DTR_CONTROL_ENABLE;
		else
			dcb.fDtrControl = DTR_CONTROL_DISABLE;
	}

	if (!SetCommState(port->hdl, &dcb))
		return SP_ERR_FAIL;
#else
	struct termios term;
	speed_t baud;
	int controlbits;

	if (tcgetattr(port->fd, &term) < 0)
		return SP_ERR_FAIL;

	switch (baudrate) {
	case 50:
		baud = B50;
		break;
	case 75:
		baud = B75;
		break;
	case 110:
		baud = B110;
		break;
	case 134:
		baud = B134;
		break;
	case 150:
		baud = B150;
		break;
	case 200:
		baud = B200;
		break;
	case 300:
		baud = B300;
		break;
	case 600:
		baud = B600;
		break;
	case 1200:
		baud = B1200;
		break;
	case 1800:
		baud = B1800;
		break;
	case 2400:
		baud = B2400;
		break;
	case 4800:
		baud = B4800;
		break;
	case 9600:
		baud = B9600;
		break;
	case 19200:
		baud = B19200;
		break;
	case 38400:
		baud = B38400;
		break;
	case 57600:
		baud = B57600;
		break;
	case 115200:
		baud = B115200;
		break;
	case 230400:
		baud = B230400;
		break;
#if !defined(__APPLE__) && !defined(__OpenBSD__)
	case 460800:
		baud = B460800;
		break;
#endif
	default:
		return SP_ERR_ARG;
	}

	if (cfsetospeed(&term, baud) < 0)
		return SP_ERR_FAIL;

	if (cfsetispeed(&term, baud) < 0)
		return SP_ERR_FAIL;

	term.c_cflag &= ~CSIZE;
	switch (bits) {
	case 8:
		term.c_cflag |= CS8;
		break;
	case 7:
		term.c_cflag |= CS7;
		break;
	default:
		return SP_ERR_ARG;
	}

	term.c_cflag &= ~CSTOPB;
	switch (stopbits) {
	case 1:
		term.c_cflag &= ~CSTOPB;
		break;
	case 2:
		term.c_cflag |= CSTOPB;
		break;
	default:
		return SP_ERR_ARG;
	}

	term.c_iflag &= ~(IXON | IXOFF | IXANY);
	term.c_cflag &= ~CRTSCTS;
	switch (flowcontrol) {
	case 0:
		/* No flow control. */
		break;
	case 1:
		term.c_cflag |= CRTSCTS;
		break;
	case 2:
		term.c_iflag |= IXON | IXOFF | IXANY;
		break;
	default:
		return SP_ERR_ARG;
	}

	term.c_iflag &= ~IGNPAR;
	term.c_cflag &= ~(PARENB | PARODD);
	switch (parity) {
	case SP_PARITY_NONE:
		term.c_iflag |= IGNPAR;
		break;
	case SP_PARITY_EVEN:
		term.c_cflag |= PARENB;
		break;
	case SP_PARITY_ODD:
		term.c_cflag |= PARENB | PARODD;
		break;
	default:
		return SP_ERR_ARG;
	}

	/* Turn off all serial port cooking. */
	term.c_iflag &= ~(ISTRIP | INLCR | ICRNL);
	term.c_oflag &= ~(ONLCR | OCRNL | ONOCR);
#if !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__)
	term.c_oflag &= ~OFILL;
#endif

	/* Disable canonical mode, and don't echo input characters. */
	term.c_lflag &= ~(ICANON | ECHO);

	/* Ignore modem status lines; enable receiver */
	term.c_cflag |= (CLOCAL | CREAD);

	/* Write the configured settings. */
	if (tcsetattr(port->fd, TCSADRAIN, &term) < 0)
		return SP_ERR_FAIL;

	if (rts != -1) {
		controlbits = TIOCM_RTS;
		if (ioctl(port->fd, rts ? TIOCMBIS : TIOCMBIC,
				&controlbits) < 0)
			return SP_ERR_FAIL;
	}

	if (dtr != -1) {
		controlbits = TIOCM_DTR;
		if (ioctl(port->fd, dtr ? TIOCMBIS : TIOCMBIC,
				&controlbits) < 0)
			return SP_ERR_FAIL;
	}
#endif

	return SP_OK;
}

int sp_last_error_code(void)
{
#ifdef _WIN32
	return GetLastError();
#else
	return errno;
#endif
}

char *sp_last_error_message(void)
{
#ifdef _WIN32
	LPVOID message;
	DWORD error = GetLastError();

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		error,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR) &message,
		0, NULL );

	return message;
#else
	return strerror(errno);
#endif
}

void sp_free_error_message(char *message)
{
#ifdef _WIN32
	LocalFree(message);
#else
	(void)message;
#endif
}
