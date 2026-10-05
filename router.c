#include "protocols.h"
#include "queue.h"
#include "lib.h"
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>

struct trie_node* create_node() {
	struct trie_node *node = (struct trie_node *)calloc(1, sizeof(struct trie_node));
	return node;
}

int get_mask_len(uint32_t mask) {
	uint32_t m = ntohl(mask);
	int len = 0;
	while (len < 32 && (m & (1 << (31 - len)))) len++;
	return len;
}

void insert_trie(struct trie_node *root, struct route_table_entry *entry) {
	struct trie_node *curr = root;
	uint32_t prefix = ntohl(entry->prefix);
	int mask_len = get_mask_len(entry->mask);

	for (int i = 0; i < mask_len; i++) {
		int bit = (prefix >> (31 - i)) & 1;
		if (curr->children[bit] == NULL) curr->children[bit] = create_node();
		curr = curr->children[bit];
	}
	curr->route = entry;
}

struct route_table_entry* lpm(struct trie_node *root, uint32_t dest_addr) {
	struct trie_node *curr = root;
	struct route_table_entry *last_valid_route = NULL;
	uint32_t dest_ip = ntohl(dest_addr);

	for (int i = 0; i < 32; i++) {
		int bit = (dest_ip >> (31 - i)) & 1;
		if (curr->children[bit] == NULL) break;
		curr = curr->children[bit];
		if (curr->route != NULL) last_valid_route = curr->route;
	}
	return last_valid_route;
}

struct arp_table_entry arp_cache[MAX_ARP_ENTRIES];
int arp_cache_size = 0;

queue q;

int send_icmp(char *old_buf, int8_t code, int8_t type, int interface) {
	char new_packet[MAX_PACKET_LEN];
	struct ether_hdr *old_eth = (struct ether_hdr *)old_buf;
	struct ip_hdr *old_ip = (struct ip_hdr *)(old_buf + sizeof(struct ether_hdr));

	struct ether_hdr *eth = (struct ether_hdr *)new_packet;
	eth->ethr_type = htons(0x0800);
	get_interface_mac(interface, eth->ethr_shost);
	memcpy(eth->ethr_dhost, old_eth->ethr_shost, 6);

	struct ip_hdr *ip = (struct ip_hdr *)(new_packet + sizeof(struct ether_hdr));
	ip->ihl = 5;
	ip->ver = 4;
	ip->tos = 0;
	ip->id = htons(4);
	ip->frag = 0;
	ip->ttl = 64;
	ip->proto = 1;
	ip->source_addr = inet_addr(get_interface_ip(interface));
	ip->dest_addr = old_ip->source_addr;

	struct icmp_hdr *icmp = (struct icmp_hdr *)(new_packet + sizeof(struct ether_hdr) + sizeof(struct ip_hdr));
	icmp->mtype = type;
	icmp->mcode = code;
	icmp->check = 0;

	size_t icmp_len = 0;
	if (type == 0) { // Echo Reply
		struct icmp_hdr *old_icmp = (struct icmp_hdr *)(old_buf + sizeof(struct ether_hdr) + sizeof(struct ip_hdr));
		icmp_len = ntohs(old_ip->tot_len) - sizeof(struct ip_hdr);
		memcpy(icmp, old_icmp, icmp_len);
		icmp->mtype = 0;
	} else {
		memcpy((char *)icmp + sizeof(struct icmp_hdr), old_ip, sizeof(struct ip_hdr) + 8);
		icmp_len = sizeof(struct icmp_hdr) + sizeof(struct ip_hdr) + 8;
	}

	ip->tot_len = htons(sizeof(struct ip_hdr) + icmp_len);
	ip->checksum = 0;
	ip->checksum = checksum((uint16_t *)ip, sizeof(struct ip_hdr));
	icmp->check = 0;
	icmp->check = checksum((uint16_t *)icmp, icmp_len);

	send_to_link(sizeof(struct ether_hdr) + ntohs(ip->tot_len), new_packet, interface);
	return 0;
}

void process_packet(char buf[], size_t len, size_t interface, struct trie_node *root) {
	struct ether_hdr *eth = (struct ether_hdr *)buf;
	struct ip_hdr *ip = (struct ip_hdr *)(buf + sizeof(struct ether_hdr));

	uint16_t old_check = ip->checksum;
	ip->checksum = 0;
	if (old_check != htons(checksum((uint16_t *)ip, sizeof(struct ip_hdr)))) return;

	if (ip->dest_addr == inet_addr(get_interface_ip(interface))) {
		struct icmp_hdr *icmp = (struct icmp_hdr *)(buf + sizeof(struct ether_hdr) + sizeof(struct ip_hdr));
		if (icmp->mtype == 8) send_icmp(buf, 0, 0, interface);
		return;
	}

	if (ip->ttl <= 1) {
		send_icmp(buf, 0, 11, interface);
		return;
	}

	struct route_table_entry *dest = lpm(root, ip->dest_addr);
	if (!dest) {
		send_icmp(buf, 0, 3, interface);
		return;
	}

	ip->ttl--;
	ip->checksum = 0;
	ip->checksum = htons(checksum((uint16_t *)ip, sizeof(struct ip_hdr)));

	int found = -1;
	for (int i = 0; i < arp_cache_size; i++) {
		if (arp_cache[i].ip == dest->next_hop) {
			found = i;
			break;
		}
	}

	if (found != -1) {
		get_interface_mac(dest->interface, eth->ethr_shost);
		memcpy(eth->ethr_dhost, arp_cache[found].mac, 6);
		send_to_link(len, buf, dest->interface);
	} else {
		struct packet_wrapper *pw = malloc(sizeof(struct packet_wrapper));
		memcpy(pw->buf, buf, len);
		pw->len = len;
		pw->next_hop = dest->next_hop;
		pw->interface = dest->interface;
		queue_enq(q, pw);

		char arp_packet[sizeof(struct ether_hdr) + sizeof(struct arp_hdr)];
		struct ether_hdr *e_hdr = (struct ether_hdr *)arp_packet;
		struct arp_hdr *a_hdr = (struct arp_hdr *)(arp_packet + sizeof(struct ether_hdr));

		e_hdr->ethr_type = htons(0x0806);
		get_interface_mac(dest->interface, e_hdr->ethr_shost);
		memset(e_hdr->ethr_dhost, 0xFF, 6);

		a_hdr->hw_type = htons(1);
		a_hdr->proto_type = htons(0x0800);
		a_hdr->hw_len = 6;
		a_hdr->proto_len = 4;
		a_hdr->opcode = htons(1);
		get_interface_mac(dest->interface, a_hdr->shwa);
		a_hdr->sprotoa = inet_addr(get_interface_ip(dest->interface));
		memset(a_hdr->thwa, 0, 6);
		a_hdr->tprotoa = dest->next_hop;

		send_to_link(sizeof(arp_packet), arp_packet, dest->interface);
	}
}

int main(int argc, char *argv[]) {
	char buf[MAX_PACKET_LEN];
	size_t len, interface;

	init(argv + 2, argc - 2);

	struct route_table_entry *rt = malloc(100000 * sizeof(struct route_table_entry));
	int rt_len = read_rtable(argv[1], rt);

	struct trie_node *root = create_node();
	for (int i = 0; i < rt_len; i++)
		insert_trie(root, &rt[i]);

	q = create_queue();

	while (1) {
		interface = recv_from_any_link(buf, &len);
		DIE(interface < 0, "recv_from_any_links");

		struct ether_hdr *eth = (struct ether_hdr *)buf;

		if (ntohs(eth->ethr_type) == 0x0800) {
			process_packet(buf, len, interface, root);
		} else if (ntohs(eth->ethr_type) == 0x0806) {
			struct arp_hdr *arp = (struct arp_hdr *)(buf + sizeof(struct ether_hdr));

			if (ntohs(arp->opcode) == 1) {
				arp->opcode = htons(2);
				uint32_t tmp_ip = arp->sprotoa;
				arp->sprotoa = arp->tprotoa;
				arp->tprotoa = tmp_ip;
				memcpy(arp->thwa, arp->shwa, 6);
				get_interface_mac(interface, arp->shwa);

				memcpy(eth->ethr_dhost, eth->ethr_shost, 6);
				get_interface_mac(interface, eth->ethr_shost);
				send_to_link(len, buf, interface);
			} else if (ntohs(arp->opcode) == 2) {
				arp_cache[arp_cache_size].ip = arp->sprotoa;
				memcpy(arp_cache[arp_cache_size].mac, arp->shwa, 6);
				arp_cache_size++;

				queue temp_q = create_queue();
				while (!queue_empty(q)) {
					struct packet_wrapper *pw = queue_deq(q);
					if (pw->next_hop == arp->sprotoa) {
						struct ether_hdr *p_eth = (struct ether_hdr *)pw->buf;
						memcpy(p_eth->ethr_dhost, arp->shwa, 6);
						get_interface_mac(pw->interface, p_eth->ethr_shost);
						send_to_link(pw->len, pw->buf, pw->interface);
						free(pw);
					} else {
						queue_enq(temp_q, pw);
					}
				}
				q = temp_q;
			}
		}
	}
}