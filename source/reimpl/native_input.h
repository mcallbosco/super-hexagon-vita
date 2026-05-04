#ifndef SHX_NATIVE_INPUT_H
#define SHX_NATIVE_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

void native_input_install_hooks(void *superhex_mod);
void native_input_set_state(int left, int right, int confirm, int back, int start);

#ifdef __cplusplus
}
#endif

#endif
