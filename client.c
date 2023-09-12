/* 
 * http://www.ibm.com/support/knowledgecenter/ssw_aix_71/com.ibm.aix.rdma/client_active_example.htm?lang=zh-tw
 * 建置：
 * cc -o client client.c -lrdmacm -libverbs
 * 
 * 用法：
 * client <servername> <val1> <val2>
 *
 * 連接至伺服器，透過「RDMA 寫入」傳送 val1 並透過「RDMA 傳送」傳送 val2，
 * 然後從伺服器接收回 val1+val2。
 */ 
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <rdma/rdma_cma.h>

enum   { 
        RESOLVE_TIMEOUT_MS = 5000, 
}; 
struct pdata { 
        uint64_t	buf_va; 
        uint32_t	buf_rkey;
}; 

int main(int argc, char   *argv[ ]) 
{
   	struct pdata					server_pdata;

   	struct rdma_event_channel		*cm_channel; 
   	struct rdma_cm_id				*cm_id; 
   	struct rdma_cm_event				*event;  
   	struct rdma_conn_param			conn_param = { };

   	struct ibv_pd					*pd; 
   	struct ibv_comp_channel			*comp_chan; 
   	struct ibv_cq					*cq; 
   	struct ibv_cq					*evt_cq; 
   	struct ibv_mr					*mr; 
   	struct ibv_qp_init_attr			qp_attr = { }; 
   	struct ibv_sge					sge; 
   	struct ibv_send_wr				send_wr = { }; 
   	struct ibv_send_wr 				*bad_send_wr; 
   	struct ibv_recv_wr				recv_wr = { }; 
   	struct ibv_recv_wr				*bad_recv_wr; 
   	struct ibv_wc					wc; 
   	void							*cq_context; 
   	struct addrinfo					*res, *t; 
   	struct addrinfo					hints = { 
   		.ai_family    = AF_INET,
   		.ai_socktype  = SOCK_STREAM
   	 };
	int								n; 
	uint32_t						*buf; 
	int								err;

      /* 設定 RDMA CM 結構 */
	cm_channel = rdma_create_event_channel(); 
	if (!cm_channel)  
		return 1; 

	err = rdma_create_id(cm_channel, &cm_id, NULL, RDMA_PS_TCP);
	if (err)  
		return err;

	n = getaddrinfo(argv[1], "20079", &hints, &res);
	if (n < 0)  
		return 1;

	/* 解析伺服器位址及路徑 */

	for (t = res; t; t = t->ai_next) {
		err = rdma_resolve_addr(cm_id, NULL, t->ai_addr, RESOLVE_TIMEOUT_MS);
		if (!err)
			break;
	}
	if (err)
		return err;

	err = rdma_get_cm_event(cm_channel, &event);
	if (err)
		return err;

	if (event->event != RDMA_CM_EVENT_ADDR_RESOLVED)
		return 1;

	rdma_ack_cm_event(event);

	err = rdma_resolve_route(cm_id, RESOLVE_TIMEOUT_MS);
	if (err)
		return err;

	err = rdma_get_cm_event(cm_channel, &event);
	if (err)
		return err;

	if (event->event != RDMA_CM_EVENT_ROUTE_RESOLVED)
		return 1; 

	rdma_ack_cm_event(event);

	/* 建立動詞物件，因為我們知道要使用哪一個裝置 */

	pd = ibv_alloc_pd(cm_id->verbs); 
	if (!pd) 
		return 1;

	comp_chan = ibv_create_comp_channel(cm_id->verbs);
	if (!comp_chan) 
		return 1;

	cq = ibv_create_cq(cm_id->verbs, 2,NULL, comp_chan, 0); 
	if (!cq) 
		return 1;

	if (ibv_req_notify_cq(cq, 0))
		return 1;

	buf = calloc(2, sizeof (uint32_t)); 
	if (!buf) 
		return 1;

	mr = ibv_reg_mr(pd, buf,2 * sizeof(uint32_t), IBV_ACCESS_LOCAL_WRITE); 
	if (!mr) 
		return 1;

	qp_attr.cap.max_send_wr = 2; 
	qp_attr.cap.max_send_sge = 1;
	qp_attr.cap.max_recv_wr = 1; 
	qp_attr.cap.max_recv_sge = 1; 

	qp_attr.send_cq        = cq;
	qp_attr.recv_cq        = cq;
	qp_attr.qp_type        = IBV_QPT_RC;

	err = rdma_create_qp(cm_id, pd, &qp_attr);
	if (err)
		return err;

	conn_param.initiator_depth = 1;
	conn_param.retry_count     = 7;

	/* 連接至伺服器 */

	err = rdma_connect(cm_id, &conn_param);
	if (err)
					return err;

	err = rdma_get_cm_event(cm_channel,&event);
	if (err)
					return err;

	if (event->event != RDMA_CM_EVENT_ESTABLISHED)
					return 1;

	memcpy(&server_pdata, event->param.conn.private_data, sizeof server_pdata);
	rdma_ack_cm_event(event);

	/* Prepost 接收 */

	sge.addr = (uintptr_t) buf; 
	sge.length = sizeof (uint32_t);
	sge.lkey = mr->lkey;

	recv_wr.wr_id =     0;                
	recv_wr.sg_list =   &sge;
	recv_wr.num_sge =   1;

	if (ibv_post_recv(cm_id->qp, &recv_wr, &bad_recv_wr))
					return 1;

	/* 寫入/傳送要新增的兩個整數 */

	buf[0] = strtoul(argv[2], NULL, 0);
	buf[1] = strtoul(argv[3], NULL, 0);
	printf("%d + %d = ", buf[0], buf[1]);
	buf[0] = htonl(buf[0]);
	buf[1] = htonl(buf[1]);

	sge.addr 					  = (uintptr_t) buf; 
	sge.length                    = sizeof (uint32_t);
	sge.lkey                      = mr->lkey;

	send_wr.wr_id                 = 1;
	send_wr.opcode                = IBV_WR_RDMA_WRITE;
	send_wr.sg_list               = &sge;
	send_wr.num_sge               = 1;
	send_wr.wr.rdma.rkey          = ntohl(server_pdata.buf_rkey);
	send_wr.wr.rdma.remote_addr   = be64toh(server_pdata.buf_va);

	if (ibv_post_send(cm_id->qp, &send_wr, &bad_send_wr))
		return 1;

	sge.addr                      = (uintptr_t) buf + sizeof (uint32_t);
	sge.length                    = sizeof (uint32_t);
	sge.lkey                      = mr->lkey;
	send_wr.wr_id                 = 2;
	send_wr.opcode                = IBV_WR_SEND;
	send_wr.send_flags            = IBV_SEND_SIGNALED;
	send_wr.sg_list               =&sge;
	send_wr.num_sge               = 1;

	if (ibv_post_send(cm_id->qp, &send_wr,&bad_send_wr))
		return 1;

	/* 等待接收完成 */

	while (1) {
		if (ibv_get_cq_event(comp_chan,&evt_cq, &cq_context))
			return 1;

		if (ibv_req_notify_cq(cq, 0))
			return 1;

		if (ibv_poll_cq(cq, 1, &wc) != 1)
			return 1;

		if (wc.status != IBV_WC_SUCCESS)
			return 1;

		if (wc.wr_id == 0) {
			printf("%d\n", ntohl(buf[0]));
			return 0;
		}
   }
   return 0;
}
