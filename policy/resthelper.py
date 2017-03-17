import os
from ipaddress import IPv4Network, IPv4Address

import netifaces

import cib


# Builds a ip network object given an input ip address and netmask


def get_network_address(ip, netmask):
    ip = IPv4Address(ip)
    netmask = IPv4Address(netmask)
    net_ip = IPv4Address(int(ip) & int(netmask))
    return IPv4Network('%s/%s' % (net_ip, netmask))


def get_if(ipaddr):
    ipaddr = IPv4Address(ipaddr)
    for en in netifaces.interfaces():
        for addr in netifaces.ifaddresses(en).get(netifaces.AF_INET, []):
            try:
                net = get_network_address(addr['addr'], addr['netmask'])
                if ipaddr in net:
                    return addr['addr']
            except ValueError as e:
                pass
    gw, en = netifaces.gateways()['default'][netifaces.AF_INET]
    return netifaces.ifaddresses(en)[netifaces.AF_INET][0]['addr']


from policy import NEATProperty, PropertyArray


def gen_cibs():
    for en in netifaces.interfaces():
        c = cib.CIBNode()
        c.uid = en
        c.description = "autogenerated CIB node for local interface %s" % en
        c.root = True
        c.filename = en + '.cib'
        c.expire = -1

        pa = PropertyArray()
        pa.add(NEATProperty(('interface', en), precedence=NEATProperty.IMMUTABLE))
        pa.add(NEATProperty(('local_interface', True), precedence=NEATProperty.IMMUTABLE))
        c.properties.add(pa)

        addr_list = []

        for af, addresses in netifaces.ifaddresses(en).items():
            if af == netifaces.AF_INET:
                af_prop = NEATProperty(('ip_version', 4), precedence=NEATProperty.IMMUTABLE)
            elif af == netifaces.AF_INET6:
                af_prop = NEATProperty(('ip_version', 6), precedence=NEATProperty.IMMUTABLE)
            else:
                continue

            for addr in addresses:
                pa = PropertyArray()
                pa.add(NEATProperty(('local_ip', addr.get('addr', 0)),precedence=NEATProperty.IMMUTABLE))
                pa.add(af_prop)
                addr_list.append(pa)

        if addr_list:
            c.properties.add(addr_list)
            yield c.json()


# see also getnifs.py from https://gist.github.com/chadmiller/5157850


import struct


def netlink_update():
    RTMGRP_LINK = 1

    NLMSG_NOOP = 1
    NLMSG_ERROR = 2

    RTM_NEWLINK = 16
    RTM_DELLINK = 17

    IFLA_IFNAME = 3

    try:
        sock = socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, socket.NETLINK_ROUTE)
    except AttributeError:
        return
    sock.bind((os.getpid(), RTMGRP_LINK))

    while True:
        data = s.recv(65535)
        msg_len, msg_type, flags, seq, pid = struct.unpack("=LHHLL", data[:16])
        print(msg_type)


if __name__ == "__main__":
    gen_cibs()
    import code

    code.interact(local=locals(), banner='resthelper')
