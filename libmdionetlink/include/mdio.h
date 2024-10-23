#ifndef _LIBMDIO_H
#define _LIBMDIO_H

#include <stdbool.h>
#include <stdint.h>
#include <linux/mdio-netlink.h>

#define ARRAY_SIZE(_a) (sizeof(_a) / sizeof((_a)[0]))
#define container_of(ptr, type, member) ({	\
	const typeof( ((type *)0)->member )	\
		*__mptr = (ptr);		\
		(type *)( (char *)__mptr - __builtin_offsetof(type,member) );})

#define MDIO_DEV_MAX 32

#define REG(_r) ((MDIO_NL_ARG_REG << 16) | ((uint16_t)(_r)))
#define IMM(_n) ((MDIO_NL_ARG_IMM << 16) | ((uint16_t)(_n)))
#define INVALID 0
#define GOTO(_from, _to) ((MDIO_NL_ARG_IMM << 16) | ((uint16_t)((_to) - (_from) - 1)))

#define INSN(_op, _a0, _a1, _a2)		\
	((struct mdio_nl_insn)			\
	{					\
		.op = MDIO_NL_OP_ ## _op,	\
		.arg0 = _a0,			\
		.arg1 = _a1,			\
		.arg2 = _a2			\
	})

struct mdio_prog {
	struct mdio_nl_insn *insns;
	int len;
};
#define MDIO_PROG_EMPTY ((struct mdio_prog) { 0 })
#define MDIO_PROG_FIXED(_insns)			\
	((struct mdio_prog)			\
	{					\
		.insns = _insns,		\
		.len = ARRAY_SIZE(_insns)	\
	})

void mdio_prog_push(struct mdio_prog *prog, struct mdio_nl_insn insn);

typedef int (*mdio_xfer_cb_t)(uint32_t *data, int len, int err, void *arg);

struct mdio_ops {
	char *bus;

	void (*usage)(FILE *fp);

	int (*push_read) (struct mdio_ops *ops, struct mdio_prog *prog,
			  uint16_t dev, uint16_t reg);
	int (*push_write)(struct mdio_ops *ops, struct mdio_prog *prog,
			  uint16_t dev, uint16_t reg, uint32_t val);
};

int mdio_xfer(const char *bus, struct mdio_prog *prog,
	      mdio_xfer_cb_t cb, void *arg);

int mdio_modprobe(void);
int mdio_init(void);

struct mdio_driver;

struct mdio_device {
	const struct mdio_driver *driver;
	const char *bus;

	struct {
		uint32_t max;
		uint8_t  stride;
		uint8_t  width;
	} mem;
};

struct mdio_driver {
	/* Mandatory */
	int (*read) (struct mdio_device *dev, struct mdio_prog *prog,
		     uint32_t reg);
	int (*write)(struct mdio_device *dev, struct mdio_prog *prog,
		     uint32_t reg, uint32_t val);
};
int bus_status(const char *bus);


#endif	/* _LIBMDIO_H */
