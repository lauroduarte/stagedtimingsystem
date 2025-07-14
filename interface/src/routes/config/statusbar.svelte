<script lang="ts">
	import { page } from '$app/state';
	import Hamburger from '~icons/tabler/menu-2';


</script>

<div class="navbar bg-base-300 sticky top-0 z-10 h-12 min-h-fit drop-shadow-lg lg:h-16">
	<div class="flex-1 flex items-center justify-left">
		<!-- Page Hamburger Icon here -->
		<label for="main-menu" class="btn btn-ghost btn-circle btn-sm drawer-button lg:hidden"
			><Hamburger class="h-6 w-auto" /></label
		>
		<span class="inline-block px-2 text-xl font-bold lg:text-2xl">{page.data.title}</span>
	</div>
</div>
