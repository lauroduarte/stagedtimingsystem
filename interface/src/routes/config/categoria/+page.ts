import type { PageLoad } from './$types';

export const load = (async ({ fetch }) => {
	return {
		title: 'Categorias'
	};
}) satisfies PageLoad;