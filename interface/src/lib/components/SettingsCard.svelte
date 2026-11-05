<script lang="ts">
	import { slide } from 'svelte/transition';
	import { cubicOut } from 'svelte/easing';
	import Down from '~icons/tabler/chevron-down';
	interface Props {
		open?: boolean;
		collapsible?: boolean;
		maxWidth?: string;
		icon?: import('svelte').Snippet;
		title?: import('svelte').Snippet;
		children?: import('svelte').Snippet;
	}

	let {
		open = $bindable(true),
		collapsible = true,
		maxWidth = 'max-w-4xl',
		icon,
		title,
		children
	}: Props = $props();
</script>

{#if collapsible}
	<div
		class="bg-base-200 rounded-box shadow-primary/50 relative flex flex-col w-full {maxWidth} self-center shadow-lg"
	>
		<div
			class="min-h-14 sm:min-h-16 flex w-full items-center justify-between space-x-2 sm:space-x-3 p-3 sm:p-5 text-lg sm:text-xl font-medium"
		>
			<span class="inline-flex items-center min-w-0 flex-1">
				{@render icon?.()}
				<span class="truncate">{@render title?.()}</span>
			</span>
			<button
				class="btn btn-circle btn-ghost btn-sm shrink-0"
				onclick={() => {
					open = !open;
				}}
			>
				<Down
					class="text-base-content h-auto w-6 transition-transform duration-300 ease-in-out {open
						? 'rotate-180'
						: ''}"
				/>
			</button>
		</div>
		{#if open}
			<div
				class="flex flex-col gap-2 p-3 sm:p-5 pt-0 w-full min-w-0"
				transition:slide|local={{ duration: 300, easing: cubicOut }}
			>
				{@render children?.()}
			</div>
		{/if}
	</div>
{:else}
	<div
		class="bg-base-200 rounded-box shadow-primary/50 relative flex flex-col w-full {maxWidth} self-center shadow-lg"
	>
		<div class="min-h-14 sm:min-h-16 w-full p-3 sm:p-5 text-lg sm:text-xl font-medium">
			<span class="inline-flex items-center min-w-0 flex-1">
				{@render icon?.()}
				<span class="truncate">{@render title?.()}</span>
			</span>
		</div>
		<div class="flex flex-col gap-2 p-3 sm:p-5 pt-0 w-full min-w-0">
			{@render children?.()}
		</div>
	</div>
{/if}
