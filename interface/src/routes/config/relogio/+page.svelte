<script lang="ts">
	import type { PageData } from '../$types';
	import Relogio from './Relogio.svelte';

	interface Props {
		data: PageData;
	}

	let { data }: Props = $props();
</script>

<div class="w-full min-w-0 max-w-5xl mx-auto px-1 sm:px-4 py-2 sm:py-6 space-y-4 sm:space-y-6">
	<Relogio />
</div>
