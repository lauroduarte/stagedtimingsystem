<script lang="ts">
	import { goto } from '$app/navigation';
	import { onMount } from 'svelte';

	onMount(() => {
		goto('/config/user');
	});
</script>

<div class="p-8 text-center">
	<p class="text-gray-500">Redirecionando para o gerenciamento de usuários...</p>
	<a href="/config/user" class="btn btn-primary btn-sm mt-4">Acessar Usuários</a>
</div>
