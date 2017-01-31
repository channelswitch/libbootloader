#include <stdint.h>

#pragma pack(push, 1)
struct list_entry {
};

struct parameter_block {
	struct list_entry load_order;
	struct list_entry memory_descriptor;
	struct list_entry boot_driver;
	uint32_t kernel_stack;
	uint32_t prcb;
	uint32_t process;
	uint32_t thread;
	uint32_t registry_length;
	uint32_t registry_base;
	uint32_t configuration_boot;
	uint32_t boot_device_name;
	uint32_t hal_device_name;
	uint32_t boot_path_name;
	uint32_t hal_path_name;
	uint32_t load_options;
	uint32_t nls_data;
	uint32_t disk_info;
	uint32_t oem_font_file;
	uint32_t setup_loader_block;
	uint32_t extension;
};
#pragma pack(pop)
