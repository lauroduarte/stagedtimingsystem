import type { PageLoad } from './$types';

export const load = (async () => {
	return {
		title: 'URL Sincronização'
	};
}) satisfies PageLoad;
