import type { Plugin } from 'vite';

export default function viteLittleFS(): Plugin[] {
	return [
		{
			name: 'vite-plugin-littlefs',
			enforce: 'post',
			apply: 'build',

			async config(config, _configEnv) {
				if (!config.build) {
					config.build = {};
				}
				if (!config.build.rollupOptions) {
					config.build.rollupOptions = {};
				}

				const output = config.build.rollupOptions.output;
				if (!output || Array.isArray(output)) {
					return;
				}

				const { assetFileNames, chunkFileNames, entryFileNames } = output;

				// Handle Server-build + Client Assets
				config.build.rollupOptions.output = {
					...output,
					assetFileNames:
						typeof assetFileNames === 'string'
							? assetFileNames.replace('.[hash]', '')
							: assetFileNames
				};

				// Handle Client-build
				if (typeof chunkFileNames === 'string' && chunkFileNames.includes('hash')) {
					config.build.rollupOptions.output = {
						...config.build.rollupOptions.output,
						chunkFileNames: chunkFileNames.replace('.[hash]', ''),
						entryFileNames:
							typeof entryFileNames === 'string'
								? entryFileNames.replace('.[hash]', '')
								: entryFileNames
					};
				}
			}
		}
	];
}
