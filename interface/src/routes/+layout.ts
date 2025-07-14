import type { LayoutLoad } from './$types';

// This can be false if you're using a fallback (i.e. SPA mode)
export const prerender = false;
export const ssr = false;

export const load = (async ({ fetch }) => {
	const result = await fetch('/rest/features');
	const item = await result.json();
	return {
		features: item,
		title: 'Staged Timing System',
		github: 'lauroduarte/staged-timing-system',
		copyright: '2025 lauroduarte',
		appName: 'Staged Timing System',
	};
}) satisfies LayoutLoad;
