// MAH: start
#include <config.h>
#include "ofpbuf.h"  // need this?
#include "packets.h"
#include "switch-port.h"
#include <errno.h>

#define THIS_MODULE VLM_datapath
#include "vlog.h"

// The following four functions must be defined for the red-black tree code.
void PortKeyDest(void* a) {
  free((unsigned int*)a);
}

int PortKeyComp(const void* a,const void* b) {
  if( *(unsigned int*)a > *(unsigned int*)b) return(1);
  if( *(unsigned int*)a < *(unsigned int*)b) return(-1);
  return(0);
}

void PortKeyPrint(const void* a) {
  printf("vport #%u",*(unsigned int*)a);
}

void PortEntryPrint(void* a) {
  struct vport_table_entry *vpe = (struct vport_table_entry *)a;
  printf("VPortTableEntry:\nvport = %u   actions_len = %u\n", vpe->vport, (unsigned)vpe->port_acts->actions_len);
}

void PortEntryDest(void *a){
  struct vport_table_entry *vpe = (struct vport_table_entry *)a;
  free_vport_table_entry(vpe);
}


void vport_table_init(struct vport_table_t *vport_table)
{
	// Create the red-black tree to store the port tables entries.
	vport_table->table = RBTreeCreate(PortKeyComp,PortKeyDest,PortEntryDest,PortKeyPrint,PortEntryPrint);
	vport_table->active_vports = 0;
	vport_table->lookup_count = 0;
	vport_table->max_vports = MAX_VPORT_TABLE_SIZE;
	vport_table->port_match_count = 0;
	vport_table->chain_match_count = 0;
}


/* Allocates and returns a new flow with room for 'actions_len' actions.
 * Returns the new flow or a null pointer on failure. */
struct vport_table_entry *
vport_table_entry_alloc(size_t actions_len)
{
    struct sw_vport_actions *svpa;
    size_t size = sizeof *svpa + actions_len;
    struct vport_table_entry *vpe = malloc(sizeof *vpe);
    if (!vpe) {
    	VLOG_ERR("could not allocate virtual port table entry\n");
        return NULL;
    }

    svpa = malloc(size);
    if (!svpa) {
    	VLOG_ERR("could not allocate actions for virtual port table entry\n");
        free(vpe);
        return NULL;
    }
    svpa->actions_len = actions_len;

    vpe->vport = 0;
    vpe->parent_port_ptr = NULL;
    vpe->packet_count = 0;
    vpe->byte_count = 0;

    vpe->port_acts = svpa;

    return vpe;
}


/* free the vport_table_entry struct.
 * remove_port_table_entry will remove an entry from the vport
 * table and call this function.
 *  */
void free_vport_table_entry(struct vport_table_entry *vpe) {
	free(vpe->port_acts);
	free(vpe);
}


/* lookup a virtual port table entry using the virtual port number.
 * The virtual port number should be between OFPP_VP_START and OFPP_VP_END. */
struct vport_table_entry
	*vport_table_lookup(struct vport_table_t *vport_table, unsigned int vport)
{
	rb_red_blk_node* newNode;

    if ((newNode = RBExactQuery(vport_table->table, &vport))) {
    	return newNode->info;
    }

	return NULL;
}


/* insert a virtual port table entry. */
int insert_vport_table_entry(struct vport_table_t *vport_table, struct vport_table_entry *vpe)
{
	unsigned int *newKey;
	struct vport_table_entry *pvpe;
	newKey = (unsigned int *)malloc(sizeof(unsigned int));


	*newKey = vpe->vport;

	// parent port should obviously not be the current port.
	if (vpe->parent_port == vpe->vport) {
        VLOG_ERR("could not insert port table entry, "
        		 "invalid parent_port %u\n", vpe->parent_port);
		return EINVAL;
	}

	// find the parent port.
	if (vpe->parent_port < OFPP_VP_START) {
		// parent port is a physical port... set the parent_port_ptr to null.
		vpe->parent_port_ptr = NULL;
	} else if (vpe->parent_port >= OFPP_VP_START && vpe->parent_port <= OFPP_VP_END) {
		// parent port is a virtual port.
		// lookup the parent port's virtual port table entry.
		pvpe = vport_table_lookup(vport_table, vpe->parent_port);
		if (pvpe == NULL) {
			VLOG_ERR("could not insert port table entry, "
					 "parent_port %u not found!\n", vpe->parent_port);
			return EINVAL;
		}
		// set the parent_port_ptr.
		vpe->parent_port_ptr = pvpe;
	/*} else if (parent_port >= OFPP_VPL_START && parent_port <= OFPP_VPL_END) {
		 // TODO handle virtual port lists...
	}*/
	} else {
		VLOG_ERR("could not insert port table entry, "
				 "invalid parent_port %u!\n", vpe->parent_port);
		return EINVAL;
	}

	// Note that with the red-black tree implementation that if
	// we run out of memory on an insert the program will exit.
	vpe->node = RBTreeInsert(vport_table->table, newKey, vpe);

	vport_table->active_vports++;

	return 0;
}

/* removes a virtual port table entry from the virtual port table. */
int remove_vport_table_entry(struct vport_table_t *vport_table, unsigned int vport)
{
	struct vport_table_entry *vpe;

	vpe = vport_table_lookup(vport_table, vport);
	if (vpe != NULL) {
		// delete will call PortEntryDest and PortKeyDest which will
		// free all memory. The call should not fail.
		RBDelete(vport_table->table, vpe->node);
		vport_table->active_vports--;
	} else {
		VLOG_ERR("could not free virtual port table entry, "
				 "virtual port %u does not exist!\n", vport);
		return EINVAL;
	}

	return 0;
}


/* increments counters for the virtual port table entry. */
void vport_used(struct vport_table_entry *vpe, struct ofpbuf *buffer)
{
    vpe->packet_count++;
    vpe->byte_count += buffer->size;
}


// MAH: end
