/*
 * K-IPCM (Kernel-IPC Manager)
 *
 *    Francesco Salvestrini <f.salvestrini@nextworks.it>
 *    Miquel Tarzan         <miquel.tarzan@i2cat.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kobject.h>
#include <linux/export.h>

#define RINA_PREFIX "kipcm"

#include "logs.h"
#include "utils.h"
#include "shim.h"
#include "kipcm.h"
#include "debug.h"

struct kipcm {
        struct shims *   shims;

        /* NOTE:
         *
         *   This internal mapping hides the lookups for id <-> ipcp and
         *   port-id <-> flow. They will be changed later. For the time
         *   being these lookups will be kept simpler
         *
         *     Francesco
         */
        struct list_head id_to_ipcp;
        struct list_head port_id_to_flow;
};

#define to_shim(O) container_of(O, struct shim, kobj)

/*
 * NOTE:
 *
 *   id_to_ipcp is a list at the moment, it will be changed to a map as soon
 *   as we get some free time. Functionalities will be the same, a bit more
 *   performance wise
 *
 *   Francesco
 */
struct id_to_ipcp {
        ipc_process_id_t       id;   /* Key */
        struct ipc_process_t * ipcp; /* Value*/
        struct list_head       list;
};

struct port_id_to_flow {
        port_id_t        id;   /* Key */
        struct flow *    flow; /* Value */
        struct list_head list;
};

static int add_id_to_ipcp_node(struct kipcm *         kipcm,
                               ipc_process_id_t       id,
                               struct ipc_process_t * ipc_process)
{
        struct id_to_ipcp * id_to_ipcp;

	LOG_DBG("Adding IPC process to the list of kipcm processes");
        id_to_ipcp = rkzalloc(sizeof(*id_to_ipcp), GFP_KERNEL);
        if (!id_to_ipcp){
		LOG_DBG("Failed creation of id_to_ipcp node");
                return -1;
	}

	LOG_DBG("Adding info to id_to_ipcp node");
        id_to_ipcp->id   = id;
        id_to_ipcp->ipcp = ipc_process;

	LOG_DBG("Adding id_to_ipcp node to the kipcm->id_to_ipcp list");
        INIT_LIST_HEAD(&id_to_ipcp->list);
        list_add(&id_to_ipcp->list, &kipcm->id_to_ipcp);
	LOG_DBG("Node added");

        return 0;
}

static struct ipc_process_t * find_ipc_process_by_id(struct kipcm *   kipcm,
                                                     ipc_process_id_t id)
{
        struct id_to_ipcp * cur;

	LOG_DBG("Find IPC process by id");
	LOG_DBG("IPC process id: %d", id);
        list_for_each_entry(cur, &kipcm->id_to_ipcp, list) {
                if (cur->id == id) {
			LOG_DBG("IPC process found: %pk", cur->ipcp);
                        return cur->ipcp;
                }
        }

	LOG_DBG("IPC process not found");
        return NULL;
}

static struct id_to_ipcp * find_ipc_node_by_id(struct kipcm *   kipcm,
					       ipc_process_id_t id)
{
        struct id_to_ipcp * cur;

	LOG_DBG("Find IPC node by id");
	LOG_DBG("IPC process id: %d", id);
        list_for_each_entry(cur, &kipcm->id_to_ipcp, list) {
                if (cur->id == id) {
			LOG_DBG("IPC process found: %pk", cur->ipcp);
                        return cur;
                }
        }

	LOG_DBG("IPC node not found");
        return NULL;
}

struct kipcm * kipcm_init(struct kobject * parent)
{
        struct kipcm * tmp;

        LOG_DBG("Initializing");

        tmp = rkzalloc(sizeof(*tmp), GFP_KERNEL);
        if (!tmp)
                return NULL;

        tmp->shims = shims_init(parent);
        if (!tmp->shims) {
                rkfree(tmp);
                return NULL;
        }

        INIT_LIST_HEAD(&tmp->id_to_ipcp);
        INIT_LIST_HEAD(&tmp->port_id_to_flow);

        LOG_DBG("Initialized successfully");

        return tmp;
}

int kipcm_fini(struct kipcm * kipcm)
{
        LOG_DBG("Finalizing");

        ASSERT(kipcm);

        /* FIXME: Destroy elements from id_to_ipcp */
        ASSERT(list_empty(&kipcm->id_to_ipcp));

        /* FIXME: Destroy elements from port_id_to_flow */
        ASSERT(list_empty(&kipcm->port_id_to_flow));

        if (shims_fini(kipcm->shims))
                return -1;

        rkfree(kipcm);

        LOG_DBG("Finalized successfully");

        return 0;
}

uint32_t kipcm_shims_version(void)
{ return shims_version(); }
EXPORT_SYMBOL(kipcm_shims_version);

struct shim * kipcm_shim_register(struct kipcm *    kipcm,
                                  const char *      name,
                                  void *            data,
                                  struct shim_ops * ops)
{ return shim_register(kipcm->shims, name, data, ops); }
EXPORT_SYMBOL(kipcm_shim_register);

int kipcm_shim_unregister(struct kipcm * kipcm,
                          struct shim *  shim)
{
        ASSERT(kipcm);
        ASSERT(shim);

        /* FIXME:
         * 
         *   We have to call _destroy on all the instances created on
         *   this shim
         *
         *     Francesco
         */
        return shim_unregister(kipcm->shims, shim);
}
EXPORT_SYMBOL(kipcm_shim_unregister);

int kipcm_ipc_create(struct kipcm *      kipcm,
                     const struct name * name,
                     ipc_process_id_t    id,
                     dif_type_t          type)
{
        struct kobject *       k;
        struct ipc_process_t * ipc_process;

        ASSERT(kipcm);
        LOG_DBG("KIPCM: %pK", kipcm);
	LOG_DBG("IPC process name: %s", name->process_name);
	LOG_DBG("IPC process id: %d", id);
	LOG_DBG("IPC process type: %d", type);

        if (!name) {
                LOG_ERR("Name is missing, cannot create ipc");
                return -1;
        }

        if (find_ipc_process_by_id(kipcm, id)) {
        	LOG_ERR("Process id %d already exists", id);

        	return -1;
        }

        switch (type) {
        case DIF_TYPE_SHIM: {
                struct shim *          shim;
                struct shim_instance * shim_instance;

                k = kset_find_obj(kipcm->shims->set, "shim-dummy");
                if (!k) {
                        LOG_ERR("Cannot find the requested shim");
                        return -1;
                }

                shim        = to_shim(k);
                ipc_process = rkzalloc(sizeof(*ipc_process), GFP_KERNEL);
                if (!ipc_process)
                        return -1;

                ipc_process->type = type;
                shim_instance = shim->ops->create(shim->data, id);
                LOG_DBG("Shim instance created: %pK", shim_instance);
                if (!shim_instance) {
                        rkfree(ipc_process);
                        return -1;
                }

                LOG_DBG("Adding instance");
                if (add_id_to_ipcp_node(kipcm, id, ipc_process)) {
                	LOG_DBG("Add failed!!!!");
                        shim->ops->destroy(shim->data, shim_instance);
                        rkfree(ipc_process);
                        return -1;
                }

                LOG_DBG("Add succeeded!!!!");
                ipc_process->data.shim_instance = shim_instance;
        }
                break;

        case DIF_TYPE_NORMAL:
                break;

        default:
                BUG();
        }

        return 0;
}

int kipcm_ipc_destroy(struct kipcm *   kipcm,
                      ipc_process_id_t id)
{
	struct ipc_process_t * ipc_process;
	struct id_to_ipcp *    id_ipcp;
	struct kobject *       k;

        ASSERT(kipcm);

	ipc_process = find_ipc_process_by_id(kipcm, id);
	if (!ipc_process) {
		LOG_ERR("IPC process %d does not exist", id);

		return -1;
	}

	id_ipcp = find_ipc_node_by_id(kipcm, id);
	if (!id_ipcp) {
		LOG_ERR("IPC process %d node does not exist", id);

		return -1;
	}

	switch (ipc_process->type) {
	case DIF_TYPE_SHIM: {
		struct shim * shim = NULL;

		k = kset_find_obj(kipcm->shims->set, "shim-dummy");
		if (!k) {
			LOG_ERR("Cannot find the requested shim");
			return -1;
		}
		shim        = to_shim(k);

		if (shim->ops->destroy(shim->data,
				ipc_process->data.shim_instance)) {
			LOG_ERR("Could not destroy shim instance %d", id);

			return -1;
		}
	}
		break;
	case DIF_TYPE_NORMAL:
		break;
	default:
		BUG();
	}

	list_del(&id_ipcp->list);
	rkfree(ipc_process);
	rkfree(id_ipcp);

	return 0;
}

int kipcm_ipc_configure(struct kipcm *                  kipcm,
                        ipc_process_id_t                id,
                        const struct ipc_process_conf * cfg)
{
        struct ipc_process_t * ipc_process;

        ASSERT(kipcm);

        ipc_process = find_ipc_process_by_id(kipcm, id);
        if (ipc_process == NULL)
                return -1;

        switch (ipc_process->type) {
        case DIF_TYPE_SHIM: {
                struct shim *        shim = NULL;
                struct kobject *     k    = NULL;
                struct shim_config * conf = NULL;

                k = kset_find_obj(kipcm->shims->set, "shim-dummy");
                if (!k) {
                        LOG_ERR("Cannot find the requested shim");
                        return -1;
                }

                shim = to_shim(k);

                /* FIXME: conf must be translated */
                LOG_MISSING;

                ipc_process->data.shim_instance =
                        shim->ops->configure(shim->data,
                                             ipc_process->data.shim_instance,
                                             conf);
        }
                break;
        case DIF_TYPE_NORMAL:
                break;
        default:
                BUG();
        }

        return 0;
}

int kipcm_flow_add(struct kipcm *   kipcm,
		   ipc_process_id_t ipc_id,
                   port_id_t        id)
{
	struct port_id_to_flow * port_flow;
	struct flow *            flow;

        ASSERT(kipcm);

	flow = rkzalloc(sizeof(*flow), GFP_KERNEL);
	if (!flow) {
		return -1;
	}

	port_flow = rkzalloc(sizeof(*port_flow), GFP_KERNEL);
	if (!port_flow) {
		rkfree(flow);
		return -1;
	}

	flow->port_id = id;
	flow->ipc_process = find_ipc_process_by_id(kipcm, ipc_id);
	if (!flow->ipc_process) {
		LOG_ERR("Couldn't find ipc_process %d", ipc_id);
		rkfree(flow);
		rkfree(port_flow);
		return -1;
	}

	switch (flow->ipc_process->type) {
	case DIF_TYPE_SHIM:
		flow->application_owned = 1;
		flow->rmt_instance      = NULL;
		break;
	case DIF_TYPE_NORMAL:
		break;
	default:
		BUG();
	}

	flow->sdu_ready = rkzalloc(sizeof(struct kfifo), GFP_KERNEL);
	if (!flow->sdu_ready) {
		rkfree(flow);
		rkfree(port_flow);

		return -1;
	}

	port_flow->id   = id;
	port_flow->flow = flow;
	INIT_LIST_HEAD(&port_flow->list);
	list_add(&port_flow->list, &kipcm->port_id_to_flow);

	return 0;
}
EXPORT_SYMBOL(kipcm_flow_add);

static struct port_id_to_flow *
retrieve_port_flow_node(struct kipcm * kipcm, port_id_t id)
{
        struct port_id_to_flow * cur;

        list_for_each_entry(cur, &kipcm->port_id_to_flow, list) {
                if (cur->id == id)
                        return cur;
        }

        return NULL;
}

int kipcm_flow_remove(struct kipcm * kipcm,
                      port_id_t      id)
{
	struct port_id_to_flow * port_flow;

        ASSERT(kipcm);

	port_flow = retrieve_port_flow_node(kipcm, id);
	if (!port_flow) {
		LOG_ERR("Couldn't retrieve the flow %d", id);

		return -1;
	}

	rkfree(port_flow->flow);
	rkfree(port_flow);

	return 0;
}
EXPORT_SYMBOL(kipcm_flow_remove);
               
int kipcm_sdu_write(struct kipcm *     kipcm,
                    port_id_t          id,
                    const struct sdu * sdu)
{
        ASSERT(kipcm);

        LOG_MISSING;

        return -1;
}
               
int kipcm_sdu_read(struct kipcm * kipcm,
                   port_id_t      id,
                   struct sdu *   sdu)
{
        ASSERT(kipcm);

        LOG_MISSING;
        
        return -1;
}

int kipcm_sdu_post(struct kipcm * kipcm,
                   port_id_t      id,
                   struct sdu *   sdu)
{
        /*
         * NOTE:
         *
         *   This function is the "southbound" interface (called by the shims)
         *   so DO NOT ADD ASSERT() here! Check the parameters instead
         *
         *   Francesco
         */

        if (!kipcm) {
                LOG_ERR("Bogus kipcm instance passed, cannot post SDU");
                return -1;
        }

        LOG_MISSING;

        return -1;
}
