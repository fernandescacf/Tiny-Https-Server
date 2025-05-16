document.getElementById('register-form').addEventListener('submit', function(event) {
    event.preventDefault(); // Prevents the form from submitting normally

    const username = document.getElementById('username').value.trim();
    const password = document.getElementById('password').value;
    const confirmPassword = document.getElementById('confirm-password').value;

    // Basic validation
    let isValid = true;
    let errorMessage = '';

    // Check if username is provided
    if (username === '') {
        isValid = false;
        errorMessage += 'Username is required.\n';
    }

    // Check if password meets minimum length requirement (e.g., 8 characters)
    if (password.length < 8) {
        isValid = false;
        errorMessage += 'Password must be at least 8 characters long.\n';
    }

    // Check if passwords match
    if (password !== confirmPassword) {
        isValid = false;
        errorMessage += 'Passwords do not match.\n';
    }

    if (!isValid) {
        alert(errorMessage);
        return; // Stop form submission
    }

    // If we've made it here, the form is valid for client-side checks
    // Now, you can send this data to the server
    const data = {
        username: username,
        password: password
    };

    fetch('/register', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify(data)
    })
    .then(response => {
        if (!response.ok) {
            throw new Error('Network response was not ok');
        }
        return response.json();
    })
    .then(data => {
        console.log('Success:', data);
        // Assuming the server sends back some success message or token
        alert('Registration successful! You can now log in.');
        window.location.href = '/login'; // Redirect to login page
    })
    .catch((error) => {
        console.error('Error:', error);
        alert('Registration failed. Please try again.');
    });
});