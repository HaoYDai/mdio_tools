#ifndef _LIBMDIO_API_H
#define _LIBMDIO_API_H

int mdio_modprobe(void);
int mdio_init(void);
int bus_status(const char *bus);
int mdio_read_reg(char *bus, unsigned char dev, unsigned char reg, uint16_t *val);
int mdio_write_reg(char *bus, unsigned char dev, unsigned char reg, uint16_t val);
#endif	/* _LIBMDIO_API_H */
