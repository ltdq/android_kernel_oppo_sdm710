#include <../drivers/staging/android/ion/msm_ion_priv.h>

atomic_long_t ion_total_size;
bool ion_cnt_enable = true;

unsigned long ion_total(void)
{
	if (!ion_cnt_enable)
		return 0;
	return (unsigned long)atomic_long_read(&ion_total_size);
}
