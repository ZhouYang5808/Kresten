#ifndef _VM_GUESTIF_H
#define _VM_GUESTIF_H

/* VM guest interface: the vm plugin is the virtualization platform
 * (Hyper-V style). Guests register with it and run as virtual machines.
 *
 *   - "kresten"  the built-in guest OS (hardware VMX, software fallback)
 *   - "linux"    the Linux subsystem (busybox, ELF + syscall emulation)
 *
 * `vm`                  runs the default (kresten) guest
 * `vm <guest>`          runs the selected guest
 * `vm list`             lists the registered guests
 */

typedef struct vm_guest {
    const char *name;               /* guest name: "kresten", "linux", ... */
    const char *desc;               /* one-line description */
    int (*init)(void);              /* called once when the vm plugin loads */
    int (*run)(const char *args);   /* run a fullscreen guest session */
} vm_guest_t;

#define VM_GUEST_MAX 8

int vm_guest_register(const vm_guest_t *g);
const vm_guest_t *vm_guest_find(const char *name);
void vm_guest_list(void);

#endif