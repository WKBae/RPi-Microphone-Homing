S = hex/shader_256.hex \
    hex/shader_512.hex \
    hex/shader_1k.hex \
    hex/shader_2k.hex \
    hex/shader_4k.hex \
    hex/shader_8k.hex \
    hex/shader_16k.hex \
    hex/shader_32k.hex \
    hex/shader_64k.hex \
    hex/shader_128k.hex \
    hex/shader_256k.hex \
    hex/shader_512k.hex \
    hex/shader_1024k.hex \
    hex/shader_2048k.hex \
    hex/shader_4096k.hex

C = mailbox.c gpu_fft.c gpu_fft_base.c gpu_fft_twiddles.c gpu_fft_shaders.c

C1D = $(C) fft_iterator.c

H1D = gpu_fft.h mailbox.h

F = -lrt -lm -ldl -lasound -g

all:	FFT_Iterator.bin

FFT_Iterator.bin:	$(S) $(C1D) $(H1D)
	gcc -o FFT_Iterator.bin $(F) $(C1D)

clean:
	rm -f *.bin
