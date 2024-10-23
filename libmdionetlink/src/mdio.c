#include <assert.h>
#include <errno.h>
#include <glob.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libmnl/libmnl.h>
#include <linux/genetlink.h>
#include <linux/mdio.h>
#include <linux/mdio-netlink.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "mdio.h"

static char buf[0x1000] __attribute__ ((aligned (NLMSG_ALIGNTO)));
static const size_t len = 0x1000;
static uint16_t mdio_family;


struct mdio_xfer_data {
	mdio_xfer_cb_t cb;
	void *arg;
};

static int parse_attrs(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	tb[type] = attr;

	return MNL_CB_OK;
}

static struct mnl_socket *msg_send(int bus, struct nlmsghdr *nlh)
{
	struct mnl_socket *nl;
	int ret;

	nl = mnl_socket_open(bus);
	if (nl == NULL) {
		perror("mnl_socket_open");
		return NULL;
	}

	ret = mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID);
	if (ret < 0) {
		perror("mnl_socket_bind");
		return NULL;
	}

	ret = mnl_socket_sendto(nl, nlh, nlh->nlmsg_len);
	if (ret < 0) {
		perror("mnl_socket_send");
		return NULL;
	}

	return nl;
}

static int msg_recv(struct mnl_socket *nl, mnl_cb_t callback, void *data, int seq)
{
	unsigned int portid;
	int ret;
	portid = mnl_socket_get_portid(nl);

	ret = mnl_socket_recvfrom(nl, buf, len);
	while (ret > 0) {
		ret = mnl_cb_run(buf, ret, seq, portid, callback, data);
		if (ret <= 0)
			break;
		ret = mnl_socket_recvfrom(nl, buf, len);
	}

	return ret;
}

static int msg_query(struct nlmsghdr *nlh, mnl_cb_t callback, void *data)
{
	unsigned int seq;
	struct mnl_socket *nl;
	int ret;

	seq = time(NULL);
	nlh->nlmsg_seq = seq;

	nl = msg_send(NETLINK_GENERIC, nlh);
	if (!nl)
		return -ENOTSUP;

	ret = msg_recv(nl, callback, data, seq);
	mnl_socket_close(nl);
	return ret;
}

static int family_id_cb(const struct nlmsghdr *nlh, void *_null)
{
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct nlattr *tb[CTRL_ATTR_MAX + 1] = {};

	mnl_attr_parse(nlh, sizeof(*genl), parse_attrs, tb);
	if (!tb[CTRL_ATTR_FAMILY_ID])
		return MNL_CB_ERROR;

	mdio_family = mnl_attr_get_u16(tb[CTRL_ATTR_FAMILY_ID]);
	return MNL_CB_OK;
}


struct nlmsghdr *msg_init(int cmd, int flags)
{
	struct genlmsghdr *genl;
	struct nlmsghdr *nlh;

	nlh = mnl_nlmsg_put_header(buf);
	if (!nlh)
		return NULL;

	nlh->nlmsg_type	 = mdio_family;
	nlh->nlmsg_flags = flags;

	genl = mnl_nlmsg_put_extra_header(nlh, sizeof(struct genlmsghdr));
	genl->cmd = cmd;
	genl->version = 1;

	return nlh;
}

int mdio_modprobe(void)
{
	int wstatus;
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		return -errno;
	} else if (!pid) {
		execl("/sbin/modprobe", "modprobe", "mdio-netlink", NULL);
		exit(1);
	}

	if (waitpid(pid, &wstatus, 0) <= 0)
		return -ECHILD;

	if (WIFEXITED(wstatus) && !WEXITSTATUS(wstatus))
		return 0;

	return -EPERM;
}

int mdio_init(void)
{
	struct genlmsghdr *genl;
	struct nlmsghdr *nlh;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type	= GENL_ID_CTRL;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;

	genl = mnl_nlmsg_put_extra_header(nlh, sizeof(struct genlmsghdr));
	genl->cmd = CTRL_CMD_GETFAMILY;
	genl->version = 1;

	mnl_attr_put_u16(nlh, CTRL_ATTR_FAMILY_ID, GENL_ID_CTRL);
	mnl_attr_put_strz(nlh, CTRL_ATTR_FAMILY_NAME, "mdio");

	return msg_query(nlh, family_id_cb, NULL);
}

void mdio_prog_push(struct mdio_prog *prog, struct mdio_nl_insn insn)
{
	prog->insns = realloc(prog->insns, (++prog->len) * sizeof(insn));
	memcpy(&prog->insns[prog->len - 1], &insn, sizeof(insn));
}

static int mdio_xfer_cb(const struct nlmsghdr *nlh, void *_xfer)
{
	struct genlmsghdr *genl = mnl_nlmsg_get_payload(nlh);
	struct mdio_xfer_data *xfer = _xfer;
	struct nlattr *tb[MDIO_NLA_MAX + 1] = {};
	int len, err, xerr = 0;
	uint32_t *data;

	mnl_attr_parse(nlh, sizeof(*genl), parse_attrs, tb);

	if (tb[MDIO_NLA_ERROR])
		xerr = (int)mnl_attr_get_u32(tb[MDIO_NLA_ERROR]);

	if (!tb[MDIO_NLA_DATA])
		return MNL_CB_ERROR;

	len = mnl_attr_get_payload_len(tb[MDIO_NLA_DATA]) / sizeof(uint32_t);
	data = mnl_attr_get_payload(tb[MDIO_NLA_DATA]);

	err = xfer->cb(data, len, xerr, xfer->arg);
	return err ? MNL_CB_ERROR : MNL_CB_OK;
}

int mdio_xfer(const char *bus, struct mdio_prog *prog,
	      mdio_xfer_cb_t cb, void *arg)
{
	struct mdio_xfer_data xfer = { .cb = cb, .arg = arg };
	struct nlmsghdr *nlh;

	nlh = msg_init(MDIO_GENL_XFER, NLM_F_REQUEST | NLM_F_ACK);
	if (!nlh)
		return -ENOMEM;

	mnl_attr_put_strz(nlh, MDIO_NLA_BUS_ID, bus);
	mnl_attr_put(nlh, MDIO_NLA_PROG, prog->len * sizeof(*prog->insns),
		     prog->insns);

	mnl_attr_put_u16(nlh, MDIO_NLA_TIMEOUT, 1000);
	return msg_query(nlh, mdio_xfer_cb, &xfer);
}

static int bus_status_cb(uint32_t *data, int len, int err, void *_null)
{
	uint16_t dev;

	if (len != MDIO_DEV_MAX * 3)
		return 1;

	printf("\e[7m%4s  %10s  %4s\e[0m\n", "DEV", "PHY-ID", "LINK");
	for (dev = 0; dev < MDIO_DEV_MAX; dev++, data += 3) {
		if (data[1] == 0xffff && data[2] == 0xffff)
			continue;

		printf("0x%2.2x  0x%8.8x  %s\n", dev,
		       (data[1] << 16) | data[2],
		       (data[0] & BMSR_LSTATUS) ? "up" : "down");
	}

	return err;
}


int bus_status(const char *bus)
{
	struct mdio_nl_insn insns[] = {
		INSN(ADD,  IMM(0), IMM(0),  REG(1)),

		INSN(READ, REG(1), IMM(1),  REG(0)),
		INSN(EMIT, REG(0),   0,         0),
		INSN(READ, REG(1), IMM(2),  REG(0)),
		INSN(EMIT, REG(0),   0,         0),
		INSN(READ, REG(1), IMM(3),  REG(0)),
		INSN(EMIT, REG(0),   0,         0),

		INSN(ADD, REG(1), IMM(1), REG(1)),
		INSN(JNE, REG(1), IMM(MDIO_DEV_MAX), IMM(-8)),
	};
	struct mdio_prog prog = MDIO_PROG_FIXED(insns);
	int err;

	err = mdio_xfer(bus, &prog, bus_status_cb, NULL);
	if (err) {
		fprintf(stderr, "ERROR: Unable to read status (%d)\n", err);
		return 1;
	}

	return 0;
}

int mdio_raw_read_cb(uint32_t *data, int len, int err, void *xpd)
{
  struct mdio_xfer_data *xfer=(struct mdio_xfer_data *)xpd;
	if (len != 1)
		return 1;

  if((uint32_t)xpd != 0){
    *(uint16_t *)xpd =(uint16_t) *data;
  }
	return err;
}

int mdio_read_reg(char *bus, unsigned char dev, unsigned char reg, uint16_t *val)
{
	struct mdio_prog prog = MDIO_PROG_EMPTY;
	mdio_xfer_cb_t cb = mdio_raw_read_cb;
	int err;


	mdio_prog_push(&prog, INSN(READ,  IMM(dev), IMM(reg),  REG(0)));
	mdio_prog_push(&prog, INSN(EMIT,  REG(0),   0,         0));

	err = mdio_xfer(bus, &prog, cb, val);
	free(prog.insns);
	if (err) {
		fprintf(stderr, "ERROR: mdio read operation failed (%d)\n", err);
		return 1;
	}

	return 0;
}

int mdio_raw_write_cb(uint32_t *data, int len, int err, void *_null)
{
  /*need debug
  printf("mdio_raw_write_cb len: 0x%4.4x err: 0x%4.4x\n", len, err);
	if (len != 0)
		return 1;*/

	return err;
}

int mdio_write_reg(char *bus, unsigned char dev, unsigned char reg, uint16_t val)
{
	struct mdio_prog prog = MDIO_PROG_EMPTY;
	mdio_xfer_cb_t cb = mdio_raw_write_cb;
	int err;

	mdio_prog_push(&prog, INSN(WRITE, IMM(dev), IMM(reg),  IMM(val)));
	mdio_prog_push(&prog, INSN(EMIT,  REG(0),   0,         0));

	err = mdio_xfer(bus, &prog, cb, NULL);
	free(prog.insns);
	if (err) {
		fprintf(stderr, "ERROR: mdio write operation failed (%d)\n", err);
		return 1;
	}

	return 0;
}


