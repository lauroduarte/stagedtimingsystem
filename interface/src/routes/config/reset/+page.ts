import type { PageLoad } from './$types';

export const load = (async () => {
	return {
		title: 'Reset / Nova Prova'
	};
}) satisfies PageLoad;
