<script lang="ts">
	import { slide } from 'svelte/transition';
	import { cubicOut } from 'svelte/easing';
	import Down from '~icons/tabler/chevron-down';
	import type { Snippet } from 'svelte';

	interface Props {
		open?: boolean;
		opened?: () => void;
		closed?: () => void;
		class?: string;
		icon?: Snippet | null;
		title?: Snippet;
		children?: Snippet;
	}

	let {
		opened = () => {},
		closed = () => {},
		open = $bindable(false),
		class: className = '',
		icon = null,
		title,
		children
	}: Props = $props();

	function openCollapsible() {
		open = !open;
		if (open) {
			opened();
		} else {
			closed();
		}
	}
</script>

<div class="{className} relative grid w-full max-w-2xl self-center overflow-hidden">
	<div class="min-h-16 flex w-full items-center justify-between space-x-3 p-4 text-xl font-medium">
		<span class="inline-flex items-baseline">
			{@render icon?.()}
			{@render title?.()}
		</span>
		<button type="button" class="btn btn-circle btn-ghost btn-sm" onclick={() => openCollapsible()}>
			<Down
				class="text-base-content h-auto w-6 transition-transform duration-300 ease-in-out {open
					? 'rotate-180'
					: ''}"
			/>
		</button>
	</div>
	{#if open}
		<div
			class="flex flex-col gap-2 p-4 pt-0"
			transition:slide|local={{ duration: 300, easing: cubicOut }}
		>
			{@render children?.()}
		</div>
	{/if}
</div>
