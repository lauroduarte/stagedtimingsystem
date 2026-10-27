import type { PageLoad } from './$types';

export const load = (async ({ fetch }) => {
	return {
		title: 'Prova'
	};
}) satisfies PageLoad;