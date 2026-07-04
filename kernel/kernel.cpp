extern "C" void kernel_main() {
	volatile char* video{ reinterpret_cast<volatile char*>(0xB8000) };
	const char* msg{ "Buffer OS" };
	for (int i{}; msg[i] != '\0'; ++i) {
		video[i * 2]     = msg[i];
		video[i * 2 + 1] = 0x0F;
	}

	while (true) { asm volatile("hlt"); }
}
